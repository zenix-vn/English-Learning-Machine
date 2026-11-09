package vn.zenix.sophia.voice

import io.github.jaredmdobson.concentus.OpusApplication
import io.github.jaredmdobson.concentus.OpusDecoder
import io.github.jaredmdobson.concentus.OpusEncoder

/** 16 kHz mono, 60 ms frames: what the server expects in both directions (config.yaml audio:). */
const val SAMPLE_RATE = 16000
const val FRAME_MS = 60
const val FRAME_SAMPLES = SAMPLE_RATE * FRAME_MS / 1000

/** Opus in pure Java (Concentus), so no native library is shipped. One instance per direction, not thread-safe. */
class OpusCodec {
    private val encoder = OpusEncoder(SAMPLE_RATE, 1, OpusApplication.OPUS_APPLICATION_AUDIO).apply {
        setBitrate(24000)
        setComplexity(5)
    }
    private val decoder = OpusDecoder(SAMPLE_RATE, 1)
    private val out = ByteArray(1275)

    /** One 60 ms frame of PCM (exactly FRAME_SAMPLES samples) -> one Opus packet. */
    fun encode(pcm: ShortArray): ByteArray {
        val n = encoder.encode(pcm, 0, FRAME_SAMPLES, out, 0, out.size)
        return out.copyOf(n)
    }

    /** One Opus packet -> PCM (a 60 ms frame from this server). Empty when the packet is not decodable. */
    fun decode(packet: ByteArray): ShortArray = if (packet.isEmpty()) ShortArray(0) else try {   // empty = "lost packet" to Opus
        val pcm = ShortArray(FRAME_SAMPLES * 2)                 // room for a 120 ms packet
        val n = decoder.decode(packet, 0, packet.size, pcm, 0, pcm.size, false)
        pcm.copyOf(n)
    } catch (e: Exception) {
        ShortArray(0)
    }
}
