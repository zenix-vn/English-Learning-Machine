package vn.zenix.sophia.data

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.TimeZone

/** One answer, kept until the server has it: [card index, quality, unix seconds] (the upload format of /sync/progress). */
data class ReviewEvent(val card: Int, val quality: Int, val ts: Long)

/**
 * The learner's progress through one pack, saved as a small JSON file. Everything that is needed to continue
 * (card states, today's new cards, study days for the streak) and to tell the server (answers not yet uploaded).
 */
class Progress(private val file: File, val packId: String, count: Int) {
    val cards: List<CardState> = List(count) { CardState() }
    private val pending = ArrayList<ReviewEvent>()
    private val days = LinkedHashSet<Long>()            // days (since 1970, local time) with at least one answer
    private var newDay = -1L
    private var newCount = 0
    var reviews = 0
        private set
    var correct = 0
        private set

    val count get() = cards.size
    val pendingEvents: List<ReviewEvent> get() = pending.toList()

    /** Answers one card; returns the interval the learner is told about. */
    fun record(index: Int, quality: Int, nowSec: Long): Int {
        val card = cards[index]
        val wasNew = !card.seen
        Sm2.grade(card, quality, nowSec)
        if (wasNew) {
            rollDay(nowSec)
            newCount++
        }
        pending += ReviewEvent(index, quality, nowSec)
        days += dayOf(nowSec)
        while (days.size > 400) days.remove(days.first())
        reviews++
        if (quality >= 3) correct++
        return card.intervalMin
    }

    fun newToday(nowSec: Long): Int {
        rollDay(nowSec)
        return newCount
    }

    private fun rollDay(nowSec: Long) {
        val d = dayOf(nowSec)
        if (d != newDay) {
            newDay = d
            newCount = 0
        }
    }

    /** Consecutive study days ending today (or yesterday: today may not have started yet). */
    fun streak(nowSec: Long): Int {
        var d = dayOf(nowSec)
        if (d !in days) d -= 1
        var n = 0
        while (d in days) { n++; d-- }
        return n
    }

    /** Takes over what the server knows about cards this phone has not studied (words taught in the AI lessons). */
    fun mergeServer(items: List<List<Number>>): Int {
        var merged = 0
        for (it in items) {
            if (it.size != 5) continue
            val i = it[0].toInt()
            if (i !in cards.indices) continue
            val c = cards[i]
            if (c.seen) continue                         // what this phone did offline wins
            c.due = it[1].toLong()
            c.intervalMin = (it[2].toDouble() * DAY_MINUTES + 0.5).toInt()
            c.easeMilli = (it[3].toDouble() * 1000 + 0.5).toInt()
            c.reps = it[4].toInt()
            c.seen = true
            merged++
        }
        return merged
    }

    fun markUploaded() = pending.clear()

    // ---------------------------------------------------------------- storage
    fun save() {
        val o = JSONObject()
        o.put("pack", packId)
        o.put("count", cards.size)
        o.put("due", JSONArray(cards.map { it.due }))
        o.put("interval", JSONArray(cards.map { it.intervalMin }))
        o.put("ease", JSONArray(cards.map { it.easeMilli }))
        o.put("reps", JSONArray(cards.map { it.reps }))
        o.put("seen", cards.joinToString("") { if (it.seen) "1" else "0" })
        o.put("newDay", newDay)
        o.put("newCount", newCount)
        o.put("days", JSONArray(days.toList()))
        o.put("reviews", reviews)
        o.put("correct", correct)
        o.put("pending", JSONArray(pending.map { JSONArray(listOf(it.card, it.quality, it.ts)) }))
        val tmp = File(file.path + ".tmp")
        tmp.writeText(o.toString())
        if (!tmp.renameTo(file)) {                       // renameTo cannot replace on some file systems
            file.delete()
            tmp.renameTo(file)
        }
    }

    private fun load(): Boolean {
        return try {
            val o = JSONObject(file.readText())
            if (o.getString("pack") != packId || o.getInt("count") != cards.size) return false
            val due = o.getJSONArray("due")
            val interval = o.getJSONArray("interval")
            val ease = o.getJSONArray("ease")
            val reps = o.getJSONArray("reps")
            val seen = o.getString("seen")
            for (i in cards.indices) {
                cards[i].due = due.getLong(i)
                cards[i].intervalMin = interval.getInt(i)
                cards[i].easeMilli = ease.getInt(i)
                cards[i].reps = reps.getInt(i)
                cards[i].seen = seen[i] == '1'
            }
            newDay = o.getLong("newDay")
            newCount = o.getInt("newCount")
            o.getJSONArray("days").let { a -> for (i in 0 until a.length()) days += a.getLong(i) }
            reviews = o.getInt("reviews")
            correct = o.getInt("correct")
            o.getJSONArray("pending").let { a ->
                for (i in 0 until a.length()) {
                    val e = a.getJSONArray(i)
                    pending += ReviewEvent(e.getInt(0), e.getInt(1), e.getLong(2))
                }
            }
            true
        } catch (e: Exception) {
            false
        }
    }

    companion object {
        /** Opens the saved progress of this pack, or starts a fresh one (progress of another pack is not reused). */
        fun open(file: File, packId: String, count: Int): Progress {
            val p = Progress(file, packId, count)
            if (file.isFile && !p.load()) {
                // unreadable or for another pack: begin again
                p.cards.forEach { it.due = 0; it.intervalMin = 0; it.easeMilli = 2500; it.reps = 0; it.seen = false }
                p.pending.clear()
                p.days.clear()
            }
            return p
        }

        fun dayOf(nowSec: Long, tzOffsetSec: Long = TimeZone.getDefault().getOffset(nowSec * 1000) / 1000L): Long =
            Math.floorDiv(nowSec + tzOffsetSec, 86400L)
    }
}
