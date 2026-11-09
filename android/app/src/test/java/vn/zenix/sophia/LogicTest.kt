package vn.zenix.sophia

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import vn.zenix.sophia.data.CardQueue
import vn.zenix.sophia.data.CardState
import vn.zenix.sophia.data.DAY_MINUTES
import vn.zenix.sophia.data.Focus
import vn.zenix.sophia.data.PackReader
import vn.zenix.sophia.data.PickKind
import vn.zenix.sophia.data.Progress
import vn.zenix.sophia.data.Quality
import vn.zenix.sophia.data.Sm2
import vn.zenix.sophia.data.formatInterval
import vn.zenix.sophia.voice.FRAME_SAMPLES
import vn.zenix.sophia.voice.OpusCodec
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.sin

class LogicTest {
    @get:Rule val tmp = TemporaryFolder()

    // ------------------------------------------------------------------ SM-2 (same numbers as firmware sm2.cc and backend apply_grade)
    @Test fun firstGoodAnswerComesBackTomorrow() {
        val c = CardState()
        Sm2.grade(c, Quality.GOOD, 1000)
        assertEquals(DAY_MINUTES, c.intervalMin)
        assertEquals(1000 + DAY_MINUTES * 60L, c.due)
        assertTrue(c.seen)
        assertEquals(1, c.reps)
    }

    @Test fun intervalsGrowOneThreeThenByEase() {
        val c = CardState()
        Sm2.grade(c, Quality.GOOD, 0)                       // 1 day
        Sm2.grade(c, Quality.GOOD, 0)                       // 3 days
        assertEquals(3 * DAY_MINUTES, c.intervalMin)
        val ease = c.easeMilli
        Sm2.grade(c, Quality.GOOD, 0)                       // 3 days x ease
        assertEquals(((3L * DAY_MINUTES * ease + 500) / 1000).toInt(), c.intervalMin)
    }

    @Test fun forgettingResetsToTenMinutes() {
        val c = CardState()
        repeat(3) { Sm2.grade(c, Quality.GOOD, 0) }
        Sm2.grade(c, Quality.FORGOT, 5000)
        assertEquals(0, c.reps)
        assertEquals(10, c.intervalMin)
        assertEquals(5000 + 600L, c.due)
        assertTrue(c.isLearning)
    }

    @Test fun easeRisesOnEasyAndNeverDropsBelowOnePointThree() {
        val easy = CardState()
        Sm2.grade(easy, Quality.EASY, 0)
        assertEquals(2600, easy.easeMilli)                  // 2.5 + 0.1
        val hard = CardState(easeMilli = 1310, reps = 5, intervalMin = DAY_MINUTES)
        repeat(5) { Sm2.grade(hard, 3, 0) }
        assertEquals(1300, hard.easeMilli)
    }

    // ------------------------------------------------------------------ queue
    private fun deck(n: Int) = List(n) { CardState() }

    @Test fun newCardsStopAtTheDailyLimit() {
        val cards = deck(5)
        assertEquals(PickKind.New, CardQueue.next(cards, 0, 0, 2, -1, Focus.New, false).kind)
        assertFalse(CardQueue.next(cards, 0, 2, 2, -1, Focus.New, false).found)
        assertTrue(CardQueue.next(cards, 0, 2, 2, -1, Focus.New, true).found)          // "Học thêm"
    }

    @Test fun mistakesComeBeforeDueCards() {
        val cards = deck(3)
        Sm2.grade(cards[0], Quality.GOOD, 0)                // due in a day
        Sm2.grade(cards[1], Quality.GOOD, 0)
        Sm2.grade(cards[2], Quality.FORGOT, 0)              // due in 10 minutes
        val now = 3L * 86400
        val pick = CardQueue.next(cards, now, 0, 10, -1, Focus.Review, false)
        assertEquals(2, pick.index)
        assertEquals(PickKind.Learning, pick.kind)
    }

    @Test fun reviewIsEmptyUntilSomethingIsDueUnlessExtra() {
        val cards = deck(2)
        Sm2.grade(cards[0], Quality.GOOD, 100)
        assertFalse(CardQueue.next(cards, 200, 0, 10, -1, Focus.Review, false).found)
        val extra = CardQueue.next(cards, 200, 0, 10, -1, Focus.Review, true)
        assertEquals(0, extra.index)
        assertEquals(PickKind.Extra, extra.kind)
    }

    @Test fun statsCountNewLeftUnderTheLimit() {
        val cards = deck(30)
        val s = CardQueue.stats(cards, 0, 4, 10)
        assertEquals(30, s.fresh)
        assertEquals(6, s.newLeft)
    }

    @Test fun intervalWording() {
        assertEquals("gặp lại sau 10 phút", formatInterval(10))
        assertEquals("gặp lại sau 1 ngày", formatInterval(DAY_MINUTES))
        assertEquals("gặp lại sau 3 ngày", formatInterval(3 * DAY_MINUTES))
    }

    // ------------------------------------------------------------------ pack format (docs/offline-learning.md)
    private fun le(values: List<Int>): ByteArray {
        val b = ByteBuffer.allocate(values.size * 4).order(ByteOrder.LITTLE_ENDIAN)
        values.forEach { b.putInt(it) }
        return b.array()
    }

    private fun pack(): PackReader? {
        val cards = listOf(
            listOf("apple", "/ˈæp.əl/", "quả táo", "I eat an apple. — Tôi ăn một quả táo."),
            listOf("tiếng", "", "sound", ""),
        )
        val bin = ByteArrayOutputStream()
        val offsets = ArrayList<Int>()
        for (c in cards) {
            offsets += bin.size()
            c.forEach { bin.write(it.toByteArray()); bin.write(0) }
        }
        offsets += bin.size()
        val audio = tmp.newFile("audio.bin").apply { writeBytes(byteArrayOf(1, 2, 3, 4, 5, 6)) }
        // card 0 has 4 bytes of audio at offset 2, card 1 has none
        return PackReader.fromBytes(bin.toByteArray(), le(offsets), le(listOf(2, 4, 0, 0)), audio)
    }

    @Test fun packReaderParsesCards() {
        val p = pack()
        assertNotNull(p)
        assertEquals(2, p!!.count)
        val a = p.card(0)
        assertEquals("apple", a.word)
        assertEquals("/ˈæp.əl/", a.ipa)
        assertEquals("quả táo", a.meaning)
        assertEquals("I eat an apple.", a.exampleEnglish)
        assertEquals("Tôi ăn một quả táo.", a.exampleVietnamese)
        assertEquals("tiếng", p.card(1).word)                 // UTF-8 survives
        assertEquals("", p.card(1).exampleVietnamese)
        assertEquals(listOf("apple", "tiếng"), p.words())
    }

    @Test fun packReaderServesAudioSegments() {
        val p = pack()!!
        assertTrue(p.hasAudio(0))
        assertFalse(p.hasAudio(1))
        assertArrayEquals(byteArrayOf(3, 4, 5, 6), p.audio(0))
        assertNull(p.audio(1))
    }

    @Test fun packReaderRejectsAnInconsistentPack() {
        val bin = "a\u0000b\u0000c\u0000d\u0000".toByteArray()
        assertNull(PackReader.fromBytes(bin, le(listOf(0, bin.size + 1)), le(listOf(0, 0)), null))   // wrong total
        assertNull(PackReader.fromBytes(bin, le(listOf(0, bin.size)), le(listOf(0, 0, 0, 0)), null))  // wrong audio index
    }

    // ------------------------------------------------------------------ progress
    @Test fun progressSurvivesSaveAndLoad() {
        val f = tmp.newFile("p.json")
        val p = Progress(f, "deck7", 10)
        p.record(3, Quality.GOOD, 1_700_000_000)
        p.record(4, Quality.FORGOT, 1_700_000_100)
        p.save()
        val q = Progress.open(f, "deck7", 10)
        assertTrue(q.cards[3].seen)
        assertEquals(DAY_MINUTES, q.cards[3].intervalMin)
        assertEquals(10, q.cards[4].intervalMin)
        assertEquals(2, q.pendingEvents.size)
        assertEquals(listOf(3, 4), q.pendingEvents.map { it.card })
        assertEquals(2, q.reviews)
        assertEquals(1, q.correct)
    }

    @Test fun progressOfAnotherPackIsNotReused() {
        val f = tmp.newFile("p2.json")
        Progress(f, "deck7", 10).also { it.record(0, Quality.GOOD, 1_700_000_000); it.save() }
        assertFalse(Progress.open(f, "deck8", 10).cards[0].seen)
        assertFalse(Progress.open(f, "deck7", 11).cards[0].seen)
    }

    @Test fun newCardsAreCountedPerDay() {
        val p = Progress(tmp.newFile("p3.json"), "d", 5)
        val t0 = 1_700_000_000L
        p.record(0, Quality.GOOD, t0)
        p.record(1, Quality.GOOD, t0 + 60)
        p.record(0, Quality.GOOD, t0 + 120)                  // a repeat: not a new card
        assertEquals(2, p.newToday(t0 + 200))
        assertEquals(0, p.newToday(t0 + 3 * 86400))
    }

    @Test fun streakCountsConsecutiveDays() {
        val p = Progress(tmp.newFile("p4.json"), "d", 5)
        val day = 86400L
        val base = 1_700_000_000L
        p.record(0, Quality.GOOD, base)
        p.record(1, Quality.GOOD, base + day)
        p.record(2, Quality.GOOD, base + 2 * day)
        assertEquals(3, p.streak(base + 2 * day + 100))
        assertEquals(3, p.streak(base + 3 * day))            // today not started yet: yesterday still counts
        assertEquals(0, p.streak(base + 5 * day))            // a gap breaks it
    }

    @Test fun serverStateOnlyFillsCardsNotStudiedHere() {
        val p = Progress(tmp.newFile("p5.json"), "d", 4)
        p.record(1, Quality.GOOD, 1_700_000_000)
        val merged = p.mergeServer(listOf(listOf(0, 1_700_100_000, 3.0, 2.4, 2), listOf(1, 1_700_100_000, 9.0, 2.0, 7), listOf(99, 0, 0, 0, 0)))
        assertEquals(1, merged)
        assertTrue(p.cards[0].seen)
        assertEquals(3 * DAY_MINUTES, p.cards[0].intervalMin)
        assertEquals(2400, p.cards[0].easeMilli)
        assertEquals(DAY_MINUTES, p.cards[1].intervalMin)    // what this phone did wins
    }

    @Test fun uploadedReviewsAreForgotten() {
        val p = Progress(tmp.newFile("p6.json"), "d", 3)
        p.record(0, Quality.GOOD, 1_700_000_000)
        assertEquals(1, p.pendingEvents.size)
        p.markUploaded()
        assertTrue(p.pendingEvents.isEmpty())
    }

    // ------------------------------------------------------------------ Opus (the server speaks 16 kHz mono, 60 ms frames)
    @Test fun opusRoundTripKeepsTheSignal() {
        val enc = OpusCodec()
        val dec = OpusCodec()
        var energyIn = 0.0
        var energyOut = 0.0
        for (frame in 0 until 12) {
            val pcm = ShortArray(FRAME_SAMPLES) { i ->
                (9000 * sin(2 * PI * 440 * (frame * FRAME_SAMPLES + i) / 16000.0)).toInt().toShort()
            }
            val packet = enc.encode(pcm)
            assertTrue("packet is small", packet.size in 1..400)
            val out = dec.decode(packet)
            assertEquals(FRAME_SAMPLES, out.size)
            if (frame >= 3) {                                 // skip the codec's start-up
                pcm.forEach { energyIn += it.toDouble() * it }
                out.forEach { energyOut += it.toDouble() * it }
            }
        }
        val ratio = energyOut / energyIn
        assertTrue("energy ratio $ratio", ratio in 0.5..1.6)
    }

    @Test fun opusIgnoresGarbage() {
        assertEquals(0, OpusCodec().decode(ByteArray(0)).size)
    }
}
