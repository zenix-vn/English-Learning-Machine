package vn.zenix.sophia.data

/**
 * Spaced repetition, the same rules as the device (firmware main/offline/sm2.cc, card_queue.cc) and the server
 * (backend/app/tools.py apply_grade), so progress made on the phone, on the device and in the AI lessons agrees.
 */
const val DAY_MINUTES = 1440

/** What is known about one card of the pack. Times are seconds since 1970. */
data class CardState(
    var due: Long = 0,
    var intervalMin: Int = 0,
    var easeMilli: Int = 2500,
    var reps: Int = 0,
    var seen: Boolean = false,
) {
    /** Still in the short steps of the same day (after a mistake, or a first sight). */
    val isLearning get() = intervalMin < DAY_MINUTES
}

/** The quality of an answer: 0 forgot, 4 remembered, 5 easy (3 = correct but hard, not used by the buttons). */
object Quality {
    const val FORGOT = 0
    const val GOOD = 4
    const val EASY = 5
}

object Sm2 {
    fun grade(card: CardState, quality: Int, nowSec: Long) {
        val q = quality.coerceIn(0, 5)
        if (q < 3) {
            card.reps = 0
            card.intervalMin = 10                               // try again in 10 minutes
        } else {
            if (card.reps < 255) card.reps++
            card.intervalMin = when (card.reps) {
                1 -> DAY_MINUTES
                2 -> 3 * DAY_MINUTES
                else -> ((card.intervalMin.toLong() * card.easeMilli + 500) / 1000).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
            }
            // ease += 0.1 - (5-q) * (0.08 + (5-q) * 0.02), in thousandths, never below 1.3
            val miss = 5 - q
            card.easeMilli = maxOf(card.easeMilli + 100 - miss * (80 + miss * 20), 1300)
        }
        card.due = nowSec + card.intervalMin * 60L
        card.seen = true
    }
}

enum class Focus { New, Review }
enum class PickKind { None, Learning, Due, New, Extra }
data class Pick(val index: Int, val kind: PickKind) {
    val found get() = kind != PickKind.None
}

data class QueueStats(val fresh: Int = 0, val learning: Int = 0, val due: Int = 0, val newLeft: Int = 0)

object CardQueue {
    /**
     * Next card for a programme. New cards: only up to the daily limit (unless `extra`). Review: mistakes first, then
     * the cards that are due; with `extra` also the ones that come due soonest.
     */
    fun next(cards: List<CardState>, now: Long, newToday: Int, newPerDay: Int, avoid: Int, focus: Focus, extra: Boolean): Pick {
        var learning = -1
        var review = -1
        var fresh = -1
        var soonest = -1
        var learningDue = Long.MAX_VALUE
        var reviewDue = Long.MAX_VALUE
        var soonestDue = Long.MAX_VALUE
        for (i in cards.indices) {
            if (i == avoid) continue
            val c = cards[i]
            if (!c.seen) {
                if (fresh < 0) fresh = i
                continue
            }
            if (c.due <= now) {
                if (c.isLearning) {
                    if (c.due < learningDue) { learningDue = c.due; learning = i }
                } else if (c.due < reviewDue) {
                    reviewDue = c.due; review = i
                }
            } else if (c.due < soonestDue) {
                soonestDue = c.due; soonest = i
            }
        }
        if (learning >= 0) return Pick(learning, PickKind.Learning)
        if (focus == Focus.New) {
            return if (fresh >= 0 && (extra || newToday < newPerDay)) Pick(fresh, PickKind.New) else Pick(-1, PickKind.None)
        }
        if (review >= 0) return Pick(review, PickKind.Due)
        if (extra && soonest >= 0) return Pick(soonest, PickKind.Extra)
        return Pick(-1, PickKind.None)
    }

    fun stats(cards: List<CardState>, now: Long, newToday: Int, newPerDay: Int): QueueStats {
        var fresh = 0
        var learning = 0
        var due = 0
        for (c in cards) {
            if (!c.seen) { fresh++; continue }
            if (c.due <= now) { if (c.isLearning) learning++ else due++ }
        }
        val left = if (newToday >= newPerDay) 0 else minOf(newPerDay - newToday, fresh)
        return QueueStats(fresh, learning, due, left)
    }
}

/** "gặp lại sau 3 ngày": what to tell the learner after an answer (same wording as the device). */
fun formatInterval(minutes: Int): String = when {
    minutes < 60 -> "gặp lại sau ${maxOf(minutes, 1)} phút"
    minutes < DAY_MINUTES -> "gặp lại sau ${(minutes + 30) / 60} giờ"
    else -> {
        val days = (minutes + 720) / DAY_MINUTES
        if (days < 60) "gặp lại sau $days ngày" else "gặp lại sau ${(days + 15) / 30} tháng"
    }
}
