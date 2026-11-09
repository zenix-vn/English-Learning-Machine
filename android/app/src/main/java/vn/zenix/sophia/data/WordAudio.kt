package vn.zenix.sophia.data

import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import java.io.File

/** Plays the recorded pronunciation of a word (an OGG/Opus clip from the pack). */
class WordAudio(private val context: Context) {
    private var player: MediaPlayer? = null

    fun play(clip: ByteArray) {
        stop()
        try {
            val file = File(context.cacheDir, "word.ogg")
            file.writeBytes(clip)
            player = MediaPlayer().apply {
                setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                setDataSource(file.path)
                setOnPreparedListener { it.start() }
                setOnCompletionListener { it.release(); if (player === it) player = null }
                setOnErrorListener { mp, _, _ -> mp.release(); if (player === mp) player = null; true }
                prepareAsync()
            }
        } catch (e: Exception) {
            player = null
        }
    }

    fun stop() {
        try { player?.release() } catch (_: Exception) {}
        player = null
    }
}
