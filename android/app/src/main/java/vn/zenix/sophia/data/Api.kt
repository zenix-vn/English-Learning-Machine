package vn.zenix.sophia.data

import android.os.Build
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import vn.zenix.sophia.BuildConfig
import java.io.File
import java.io.IOException
import java.security.MessageDigest
import java.util.concurrent.TimeUnit

class ApiException(message: String, val code: Int = 0) : Exception(message)

/** What the server said when this phone registered (the same /ota/ call the device makes at start-up). */
sealed interface Registration {
    data class Ready(val wsUrl: String, val token: String) : Registration
    /** Waiting for an admin: the code to type into the admin page. */
    data class Pending(val code: String, val wsUrl: String, val token: String) : Registration
    data class Blocked(val message: String) : Registration
}

data class PackFile(val name: String, val size: Long, val sha256: String)
data class PackManifest(val packId: String, val deckId: Int, val count: Int, val totalBytes: Long, val files: List<PackFile>, val json: String)

/** The HTTP side of the server (backend/app/main.py and sync.py). Every call runs on the IO dispatcher. */
class Api(private val prefs: Prefs) {
    val http: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(40, TimeUnit.SECONDS)
        .pingInterval(25, TimeUnit.SECONDS)
        .build()

    private fun base(path: String) = prefs.serverUrl.trimEnd('/') + path

    private fun Request.Builder.device(auth: Boolean = false): Request.Builder {
        header("Device-Id", prefs.deviceId)
        header("Client-Id", prefs.clientId)
        header("User-Agent", "Sophia-Android/${BuildConfig.VERSION_NAME}")
        if (auth && prefs.token.isNotEmpty()) header("Authorization", "Bearer ${prefs.token}")
        return this
    }

    private fun <T> call(request: Request, block: (okhttp3.Response) -> T): T = try {
        http.newCall(request).execute().use(block)
    } catch (e: IOException) {
        throw ApiException("Không kết nối được máy chủ")
    }

    /** Registers (or refreshes) this phone and learns the WebSocket address, the token and whether it is activated. */
    suspend fun register(): Registration = withContext(Dispatchers.IO) {
        val body = JSONObject()
            .put("application", JSONObject().put("name", "sophia-android").put("version", BuildConfig.VERSION_NAME))
            .put("board", JSONObject().put("type", "android").put("name", "${Build.MANUFACTURER} ${Build.MODEL}"))
            .toString().toRequestBody("application/json".toMediaType())
        val req = Request.Builder().url(base("/ota/")).device().post(body).build()
        call(req) { r ->
            val text = r.body?.string().orEmpty()
            if (r.code == 429) throw ApiException("Máy chủ từ chối vì đăng ký quá nhiều, hãy thử lại sau", 429)
            if (!r.isSuccessful) throw ApiException("Máy chủ báo lỗi ${r.code}", r.code)
            val o = JSONObject(text)
            val ws = o.optJSONObject("websocket")
            val wsUrl = ws?.optString("url").orEmpty()
            val token = ws?.optString("token").orEmpty()
            val activation = o.optJSONObject("activation")
            when {
                activation == null -> Registration.Ready(wsUrl, token)
                activation.optString("challenge") == "blocked" -> Registration.Blocked(activation.optString("message"))
                else -> Registration.Pending(activation.optString("code"), wsUrl, token)
            }
        }
    }

    /** true = activated, false = still waiting. Throws when blocked. */
    suspend fun isActivated(): Boolean = withContext(Dispatchers.IO) {
        val req = Request.Builder().url(base("/ota/activate")).device().post(ByteArray(0).toRequestBody()).build()
        call(req) { r ->
            when (r.code) {
                200 -> true
                202 -> false
                403 -> throw ApiException("Thiết bị đã bị khóa. Hãy liên hệ quản trị viên.", 403)
                else -> throw ApiException("Máy chủ báo lỗi ${r.code}", r.code)
            }
        }
    }

    // ------------------------------------------------------------------ offline pack
    private fun check(r: okhttp3.Response) {
        when (r.code) {
            200, 206 -> return
            401, 403 -> throw ApiException("Thiết bị chưa được kích hoạt hoặc gói cước đã hết hạn", r.code)
            404 -> throw ApiException("Máy chủ chưa dựng gói học cho bộ từ của bạn", 404)
            else -> throw ApiException("Máy chủ báo lỗi ${r.code}", r.code)
        }
    }

    suspend fun manifest(): PackManifest = withContext(Dispatchers.IO) {
        val req = Request.Builder().url(base("/sync/manifest")).device(auth = true).build()
        call(req) { r ->
            check(r)
            val pack = JSONObject(r.body!!.string()).getJSONObject("pack")
            val files = pack.getJSONArray("files")
            PackManifest(
                packId = pack.getString("pack_id"),
                deckId = pack.optInt("deck_id"),
                count = pack.optInt("count"),
                totalBytes = pack.optLong("total_bytes"),
                files = List(files.length()) {
                    val f = files.getJSONObject(it)
                    PackFile(f.getString("name"), f.getLong("size"), f.getString("sha256"))
                },
                json = pack.toString(),
            )
        }
    }

    /** Downloads one pack file, checks its size and sha256. `onBytes` gets the bytes read so far. */
    suspend fun download(packId: String, file: PackFile, dest: File, onBytes: (Long) -> Unit) = withContext(Dispatchers.IO) {
        val req = Request.Builder().url(base("/sync/file/$packId/${file.name}")).device(auth = true).build()
        call(req) { r ->
            check(r)
            val digest = MessageDigest.getInstance("SHA-256")
            var total = 0L
            r.body!!.byteStream().use { input ->
                dest.outputStream().use { out ->
                    val buf = ByteArray(32 * 1024)
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n)
                        digest.update(buf, 0, n)
                        total += n
                        onBytes(total)
                    }
                }
            }
            val hex = digest.digest().joinToString("") { "%02x".format(it) }
            if (total != file.size || !hex.equals(file.sha256, ignoreCase = true)) {
                dest.delete()
                throw ApiException("File ${file.name} tải về bị hỏng, hãy thử lại")
            }
        }
    }

    /** Progress made online (words taught in the AI lessons): [card, due, interval_days, ease, reps] rows. */
    suspend fun serverState(packId: String): List<List<Number>> = withContext(Dispatchers.IO) {
        val req = Request.Builder().url(base("/sync/state?pack=$packId")).device(auth = true).build()
        call(req) { r ->
            check(r)
            val items = JSONObject(r.body!!.string()).getJSONArray("items")
            List(items.length()) { i ->
                val row = items.getJSONArray(i)
                List<Number>(row.length()) { j -> row.getDouble(j) }
            }
        }
    }

    suspend fun uploadProgress(packId: String, events: List<ReviewEvent>) = withContext(Dispatchers.IO) {
        events.chunked(500).forEach { batch ->
            val payload = JSONObject()
                .put("pack", packId)
                .put("events", JSONArray(batch.map { JSONArray(listOf(it.card, it.quality, it.ts)) }))
                .toString().toRequestBody("application/json".toMediaType())
            val req = Request.Builder().url(base("/sync/progress")).device(auth = true).post(payload).build()
            call(req) { r -> check(r) }
        }
    }
}
