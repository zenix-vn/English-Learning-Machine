package vn.zenix.sophia.data

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

data class Card(val word: String, val ipa: String, val meaning: String, val example: String) {
    /** The example sentence splits into English and Vietnamese at " — ". */
    val exampleEnglish: String get() = example.substringBefore(" — ").trim()
    val exampleVietnamese: String get() = if (" — " in example) example.substringAfter(" — ").trim() else ""
}

/**
 * The offline pack the server builds for the learner's deck (backend/app/packbuild.py, docs/offline-learning.md):
 *   cards.bin  per card  word NUL ipa NUL meaning NUL example NUL  (UTF-8)
 *   cards.idx  uint32 little-endian offsets, one more than cards, the last is the size of cards.bin
 *   audio.idx  per card  uint32 offset, uint32 length into audio.bin (0 = no audio)
 *   audio.bin  one OGG/Opus clip per word
 */
class PackReader private constructor(
    private val cards: ByteArray,
    private val cardsIdx: IntArray,
    private val audioIdx: IntArray,
    private val audioFile: File?,
) {
    val count: Int get() = cardsIdx.size - 1

    fun card(index: Int): Card {
        val begin = cardsIdx[index]
        val end = cardsIdx[index + 1]
        val parts = ArrayList<String>(4)
        var start = begin
        for (i in begin until end) {
            if (cards[i].toInt() == 0) {
                parts += String(cards, start, i - start, Charsets.UTF_8)
                start = i + 1
            }
        }
        while (parts.size < 4) parts += ""
        return Card(parts[0], parts[1], parts[2], parts[3])
    }

    /** Just the headword of every card, in card order (for the dictionary). */
    fun words(): List<String> = List(count) { i ->
        val begin = cardsIdx[i]
        var end = begin
        while (end < cardsIdx[i + 1] && cards[end].toInt() != 0) end++
        String(cards, begin, end - begin, Charsets.UTF_8)
    }

    fun hasAudio(index: Int): Boolean = audioFile != null && index in 0 until count && audioIdx[2 * index + 1] != 0

    fun audio(index: Int): ByteArray? {
        if (!hasAudio(index)) return null
        val offset = audioIdx[2 * index].toLong() and 0xFFFFFFFFL
        val length = audioIdx[2 * index + 1]
        if (length <= 0 || length > MAX_CLIP) return null
        return RandomAccessFile(audioFile, "r").use { f ->
            if (offset + length > f.length()) return null
            val out = ByteArray(length)
            f.seek(offset)
            f.readFully(out)
            out
        }
    }

    companion object {
        private const val MAX_CLIP = 64 * 1024

        fun fromBytes(cards: ByteArray, cardsIdx: ByteArray, audioIdx: ByteArray, audioFile: File?): PackReader? {
            val idx = words(cardsIdx)
            if (idx.size < 2 || idx.last() != cards.size) return null
            val aidx = words(audioIdx)
            if (aidx.size != 2 * (idx.size - 1)) return null
            return PackReader(cards, idx, aidx, audioFile)
        }

        fun open(dir: File): PackReader? = try {
            fromBytes(
                File(dir, "cards.bin").readBytes(), File(dir, "cards.idx").readBytes(),
                File(dir, "audio.idx").readBytes(), File(dir, "audio.bin").takeIf { it.isFile },
            )
        } catch (e: Exception) {
            null
        }

        private fun words(bytes: ByteArray): IntArray {
            val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            return IntArray(bytes.size / 4) { buf.getInt(it * 4) }
        }
    }
}
