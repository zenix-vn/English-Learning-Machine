package vn.zenix.sophia.data

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File

data class SyncResult(val ok: Boolean, val message: String, val packChanged: Boolean = false)

/**
 * The words on the phone: the pack downloaded from the server and the learner's progress through it.
 * Call every method from the main thread (the study screens do); the slow parts hop to the IO dispatcher themselves.
 */
class Library(context: Context, private val api: Api, private val prefs: Prefs) {
    private val root = File(context.filesDir, "pack")
    private val current = File(root, "current")
    private val temp = File(root, "tmp")

    var pack: PackReader? = null
        private set
    var progress: Progress? = null
        private set
    private var packId = ""

    val loaded get() = pack != null && progress != null
    private var wordsCache: List<String>? = null

    /** The headword of every card, in card order (cached until another pack is loaded). */
    fun words(): List<String> = wordsCache ?: pack?.words().orEmpty().also { wordsCache = it }

    private fun nowSec() = System.currentTimeMillis() / 1000

    /** Opens the installed pack and the saved progress. false when no pack has been downloaded yet. */
    suspend fun load(): Boolean = withContext(Dispatchers.IO) {
        val manifest = File(current, "manifest.json")
        if (!manifest.isFile) return@withContext false
        try {
            val mf = JSONObject(manifest.readText())
            val reader = PackReader.open(current) ?: return@withContext false
            val key = "deck${mf.optInt("deck_id")}"
            val p = Progress.open(File(root, "progress_$key.json"), key, reader.count)
            packId = mf.getString("pack_id")
            pack = reader
            progress = p
            wordsCache = null
            true
        } catch (e: Exception) {
            false
        }
    }

    fun installedPackId(): String? = if (loaded) packId else null

    fun stats(): QueueStats {
        val p = progress ?: return QueueStats()
        val now = nowSec()
        return CardQueue.stats(p.cards, now, p.newToday(now), prefs.newPerDay)
    }

    suspend fun save() {
        val p = progress ?: return
        withContext(Dispatchers.IO) { p.save() }
    }

    /**
     * Downloads the pack of the learner's deck when it is new (or the first one), then exchanges progress with the
     * server: takes what the AI lessons taught, sends what was studied here. `onStatus(text, percent or null)`.
     */
    suspend fun sync(onStatus: (String, Int?) -> Unit): SyncResult {
        try {
            onStatus("Đang kiểm tra bộ từ…", null)
            val mf = api.manifest()
            var changed = false
            if (!loaded || packId != mf.packId) {
                install(mf, onStatus)
                changed = true
                if (!load()) return SyncResult(false, "Không mở được bộ từ vừa tải")
            }
            val p = progress!!
            onStatus("Đang nhận tiến độ từ máy chủ…", null)
            val merged = p.mergeServer(api.serverState(packId))
            val pending = p.pendingEvents
            if (pending.isNotEmpty()) {
                onStatus("Đang gửi ${pending.size} lượt ôn…", null)
                api.uploadProgress(packId, pending)
                p.markUploaded()
            }
            save()
            val text = buildString {
                append(if (changed) "Đã tải bộ từ mới (${mf.count} từ). " else "Bộ từ đã là bản mới nhất. ")
                if (merged > 0) append("Nhận $merged từ đã học với Sophia. ")
                if (pending.isNotEmpty()) append("Đã gửi ${pending.size} lượt ôn.")
            }.trim()
            return SyncResult(true, text, changed)
        } catch (e: ApiException) {
            return SyncResult(false, e.message ?: "Lỗi đồng bộ")
        } catch (e: Exception) {
            return SyncResult(false, "Lỗi đồng bộ: ${e.message}")
        }
    }

    private suspend fun install(mf: PackManifest, onStatus: (String, Int?) -> Unit) {
        withContext(Dispatchers.IO) {
            root.mkdirs()
            val free = root.usableSpace
            if (free < mf.totalBytes + 16L * 1024 * 1024) throw ApiException("Bộ nhớ điện thoại không đủ chỗ cho bộ từ")
            temp.deleteRecursively()
            temp.mkdirs()
        }
        var done = 0L
        for ((i, f) in mf.files.withIndex()) {
            val before = done
            api.download(mf.packId, f, File(temp, f.name)) { bytes ->
                val pct = if (mf.totalBytes > 0) ((before + bytes) * 100 / mf.totalBytes).toInt().coerceIn(0, 100) else null
                onStatus("Đang tải bộ từ (${i + 1}/${mf.files.size})", pct)
            }
            done += f.size
        }
        withContext(Dispatchers.IO) {
            File(temp, "manifest.json").writeText(mf.json)
            current.deleteRecursively()
            if (!temp.renameTo(current)) throw ApiException("Không cài được bộ từ mới")
        }
    }

    /** Forget the pack and the progress (used when the phone is unlinked from the server). */
    suspend fun wipe() = withContext(Dispatchers.IO) {
        root.deleteRecursively()
        pack = null
        progress = null
        packId = ""
        wordsCache = null
    }
}
