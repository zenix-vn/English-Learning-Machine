package vn.zenix.sophia.voice

import android.annotation.SuppressLint
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okhttp3.OkHttpClient
import okio.ByteString
import okio.ByteString.Companion.toByteString
import org.json.JSONObject
import vn.zenix.sophia.data.Prefs
import java.util.concurrent.LinkedBlockingQueue

/** What the learner sees while talking to Sophia. */
enum class Phase { Idle, Connecting, Thinking, Listening, Speaking, Error }

data class VoiceUi(
    val phase: Phase = Phase.Connecting,
    val userText: String = "",
    val botText: String = "",
    val error: String = "",
)

/**
 * A voice conversation with the server, speaking the same protocol as the device (backend/app/session.py):
 * WebSocket text messages for control, Opus packets as binary frames in both directions.
 *
 *   hello -> listen/detect(openText)   the opening line picks the mode (Học Online, Trò chuyện, Phiên dịch, Phát âm)
 *   server: tts start, sentence_start (text), Opus frames, tts stop     Sophia speaks
 *   client: listen start (auto) + Opus frames                          the server decides when the sentence is over
 *   server: stt (what it heard)                                        then the next tts start
 *
 * Microphone and speaker are never open together, so Sophia does not hear herself.
 */
class VoiceClient(private val http: OkHttpClient, private val prefs: Prefs) {
    private val _state = MutableStateFlow(VoiceUi())
    val state: StateFlow<VoiceUi> = _state.asStateFlow()

    private var ws: WebSocket? = null
    private var sessionId = ""
    private var openText = ""
    @Volatile private var closing = false
    @Volatile private var echoSeen = false
    @Volatile private var mic: Mic? = null
    private var player: Player? = null

    private val phase get() = _state.value.phase

    // ------------------------------------------------------------------ session
    fun start(openText: String) {
        this.openText = openText
        closing = false
        echoSeen = false
        _state.value = VoiceUi(Phase.Connecting)
        player?.release()
        player = Player(::onTurnEnd)
        val url = prefs.wsUrl.ifEmpty { prefs.serverUrl.replaceFirst("http", "ws") + "/ws/" }
        val request = Request.Builder().url(url)
            .header("Authorization", "Bearer ${prefs.token}")
            .header("Protocol-Version", "1")
            .header("Device-Id", prefs.deviceId)
            .header("Client-Id", prefs.clientId)
            .build()
        ws = http.newWebSocket(request, listener)
    }

    fun stop() {
        closing = true
        stopMic()
        ws?.close(1000, "bye")
        ws = null
        player?.release()
        player = null
    }

    fun retry() {
        stop()
        start(openText)
    }

    /** The big button: stop talking, talk, or interrupt Sophia, depending on what is going on. */
    fun toggleTalk() {
        when (phase) {
            Phase.Listening -> {
                stopMic()
                send(JSONObject().put("type", "listen").put("state", "stop"))
                _state.update { it.copy(phase = Phase.Idle) }
            }
            Phase.Speaking -> {
                player?.flush()
                send(JSONObject().put("type", "abort"))
                startListening()
            }
            Phase.Idle -> startListening()
            Phase.Error -> retry()
            Phase.Connecting, Phase.Thinking -> Unit
        }
    }

    // ------------------------------------------------------------------ messages
    private fun send(json: JSONObject) {
        json.put("session_id", sessionId)
        ws?.send(json.toString())
    }

    private val listener = object : WebSocketListener() {
        override fun onOpen(webSocket: WebSocket, response: Response) {
            val hello = JSONObject()
                .put("type", "hello").put("version", 1).put("transport", "websocket")
                .put("features", JSONObject().put("mcp", false))
                .put("audio_params", JSONObject().put("format", "opus").put("sample_rate", SAMPLE_RATE)
                    .put("channels", 1).put("frame_duration", FRAME_MS))
            webSocket.send(hello.toString())
        }

        override fun onMessage(webSocket: WebSocket, text: String) {
            val m = try { JSONObject(text) } catch (e: Exception) { return }
            when (m.optString("type")) {
                "hello" -> {
                    sessionId = m.optString("session_id")
                    _state.update { it.copy(phase = Phase.Thinking) }
                    send(JSONObject().put("type", "listen").put("state", "detect").put("text", openText))
                }
                "stt" -> {
                    // the server echoes the line that opened the mode ("Chế độ phiên dịch."): not something the learner said
                    if (!echoSeen && m.optString("text").trim() == openText) { echoSeen = true; return }
                    stopMic()                                          // the server has the sentence
                    _state.update { it.copy(phase = Phase.Thinking, userText = m.optString("text"), botText = "") }
                }
                "tts" -> when (m.optString("state")) {
                    "start" -> _state.update { it.copy(phase = Phase.Speaking) }
                    "sentence_start" -> {
                        val sentence = m.optString("text").trim()
                        if (sentence.isNotEmpty() && sentence != CUE) {
                            _state.update { it.copy(botText = (it.botText + " " + sentence).trim()) }
                        }
                    }
                    "stop" -> player?.endOfTurn() ?: onTurnEnd()
                }
            }
        }

        override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
            val pcm = decoder.decode(bytes.toByteArray())
            if (pcm.isNotEmpty()) player?.play(pcm)
        }

        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) = lost("Kết nối đã đóng")

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
            lost(
                when (response?.code) {
                    401 -> "Phiên đăng nhập hết hạn, hãy kết nối lại máy chủ"
                    403 -> "Thiết bị chưa được kích hoạt hoặc đã bị khóa"
                    else -> "Mất kết nối với máy chủ"
                },
            )
        }
    }

    // the decoder is used only by the WebSocket reader thread
    private val decoder = OpusCodec()

    private fun lost(message: String) {
        if (closing) return
        stopMic()
        player?.flush()
        _state.update { it.copy(phase = Phase.Error, error = message) }
    }

    /** Sophia has finished speaking (and the sound has played out): the learner's turn. */
    private fun onTurnEnd() {
        if (closing || phase == Phase.Error) return
        startListening()
    }

    // ------------------------------------------------------------------ microphone
    private fun startListening() {
        if (mic != null || closing || ws == null) return
        send(JSONObject().put("type", "listen").put("state", "start").put("mode", "auto"))
        val m = Mic()
        mic = m
        m.start()
        _state.update { it.copy(phase = Phase.Listening) }
    }

    private fun stopMic() {
        mic?.active = false
        mic = null
    }

    private inner class Mic : Thread("sophia-mic") {
        @Volatile var active = true

        @SuppressLint("MissingPermission")          // the screen asks for RECORD_AUDIO before it starts a conversation
        override fun run() {
            val min = AudioRecord.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
            val rec = try {
                AudioRecord(
                    MediaRecorder.AudioSource.VOICE_COMMUNICATION, SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT, maxOf(min, FRAME_SAMPLES * 4),
                )
            } catch (e: Exception) {
                fail("Không mở được micro")
                return
            }
            if (rec.state != AudioRecord.STATE_INITIALIZED) {
                rec.release()
                fail("Không mở được micro. Hãy cho phép ứng dụng dùng micro.")
                return
            }
            val codec = OpusCodec()
            val frame = ShortArray(FRAME_SAMPLES)
            try {
                rec.startRecording()
                while (active) {
                    var got = 0
                    while (got < FRAME_SAMPLES && active) {
                        val n = rec.read(frame, got, FRAME_SAMPLES - got)
                        if (n < 0) { fail("Micro bị ngắt"); return }
                        got += n
                    }
                    if (got == FRAME_SAMPLES && active) ws?.send(codec.encode(frame).toByteString())
                }
            } finally {
                try { rec.stop() } catch (_: Exception) {}
                rec.release()
            }
        }

        private fun fail(message: String) {
            if (mic === this) mic = null
            _state.update { it.copy(phase = Phase.Error, error = message) }
        }
    }

    // ------------------------------------------------------------------ speaker
    /** Plays Sophia's Opus frames in order. `endOfTurn` is called once everything before it has been heard. */
    private class Player(private val onTurnEnd: () -> Unit) {
        private val queue = LinkedBlockingQueue<ShortArray>()
        private val end = ShortArray(0)
        private val track: AudioTrack
        @Volatile private var running = true
        private var playEndNs = 0L

        init {
            val min = AudioTrack.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
            track = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build(),
                )
                .setAudioFormat(
                    AudioFormat.Builder().setSampleRate(SAMPLE_RATE).setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT).build(),
                )
                .setBufferSizeInBytes(maxOf(min, SAMPLE_RATE * 2 / 2))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
            track.play()
            Thread({ loop() }, "sophia-speaker").start()
        }

        fun play(pcm: ShortArray) { queue.put(pcm) }
        fun endOfTurn() { queue.put(end) }

        /** Drops what has not been played (the learner interrupted). */
        fun flush() {
            queue.clear()
            track.pause()
            track.flush()
            track.play()
            playEndNs = 0
        }

        fun release() {
            running = false
            queue.put(end)
            try { track.stop() } catch (_: Exception) {}
            track.release()
        }

        private fun loop() {
            while (running) {
                val chunk = queue.take()
                if (!running) return
                if (chunk === end) {
                    // wait for what is in the track's buffer to be heard, so the microphone does not catch the tail
                    val wait = (playEndNs - System.nanoTime()) / 1_000_000 + 250
                    if (wait > 0) Thread.sleep(minOf(wait, 4000))
                    if (running) onTurnEnd()
                    continue
                }
                val now = System.nanoTime()
                playEndNs = maxOf(playEndNs, now) + chunk.size * 1_000_000_000L / SAMPLE_RATE
                try { track.write(chunk, 0, chunk.size) } catch (e: Exception) { return }
            }
        }
    }

    companion object {
        /** The "your turn" line the interpreter says after each translation: spoken, not shown (see session.py). */
        const val CUE = "Mời bạn nói tiếp."
    }
}
