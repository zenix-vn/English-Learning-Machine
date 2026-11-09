package vn.zenix.sophia

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import vn.zenix.sophia.voice.FRAME_SAMPLES
import vn.zenix.sophia.voice.OpusCodec
import java.io.File

/**
 * The firmware's own voice prompts (the .ogg files in firmware/main/assets/locales) are 16 kHz Opus with 60 ms frames, the same
 * stream format the server sends: they must decode with the phone's Opus codec.
 */
class OggOpusTest {
    /** Splits an Ogg file into packets (lacing values of 255 continue a packet over segments and pages). */
    private fun packets(data: ByteArray): List<ByteArray> {
        val out = ArrayList<ByteArray>()
        var pos = 0
        var partial = ByteArray(0)
        while (pos + 27 <= data.size && String(data, pos, 4, Charsets.ISO_8859_1) == "OggS") {
            val segments = data[pos + 26].toInt() and 0xFF
            val table = (0 until segments).map { data[pos + 27 + it].toInt() and 0xFF }
            var at = pos + 27 + segments
            for (len in table) {
                partial += data.copyOfRange(at, at + len)
                at += len
                if (len < 255) { out += partial; partial = ByteArray(0) }
            }
            pos = at
        }
        return out
    }

    @Test fun firmwarePromptDecodes() {
        val file = File("../../firmware/main/assets/locales/vi-VN/connected.ogg")
        assumeTrue("firmware prompt not found (run from the repository)", file.isFile)
        val audio = packets(file.readBytes()).drop(2)             // OpusHead and OpusTags come first
        assertTrue("has audio packets", audio.size > 5)
        val codec = OpusCodec()
        var energy = 0.0
        for (p in audio) {
            val pcm = codec.decode(p)
            assertEquals(FRAME_SAMPLES, pcm.size)                // 60 ms at 16 kHz
            pcm.forEach { energy += it.toDouble() * it }
        }
        assertTrue("decoded speech is not silent", energy > 1e6)
    }
}
