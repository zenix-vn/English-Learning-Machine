package vn.zenix.sophia.data

import android.content.Context
import android.provider.Settings
import vn.zenix.sophia.BuildConfig
import java.util.UUID

/** Small settings kept on the phone. */
class Prefs(context: Context) {
    private val sp = context.getSharedPreferences("sophia", Context.MODE_PRIVATE)

    var serverUrl: String
        get() = sp.getString("server", null) ?: BuildConfig.DEFAULT_SERVER
        set(v) = sp.edit().putString("server", normalize(v)).apply()

    /** The id this phone registers with (like the MAC address of the device). Stable across launches. */
    val deviceId: String = sp.getString("device_id", null) ?: run {
        val androidId = Settings.Secure.getString(context.contentResolver, Settings.Secure.ANDROID_ID)
            ?.takeIf { it.isNotBlank() } ?: UUID.randomUUID().toString().replace("-", "").take(16)
        "android-$androidId".also { sp.edit().putString("device_id", it).apply() }
    }

    val clientId: String = sp.getString("client_id", null) ?: UUID.randomUUID().toString().also {
        sp.edit().putString("client_id", it).apply()
    }

    var token: String
        get() = sp.getString("token", "") ?: ""
        set(v) = sp.edit().putString("token", v).apply()

    var wsUrl: String
        get() = sp.getString("ws_url", "") ?: ""
        set(v) = sp.edit().putString("ws_url", v).apply()

    /** The server accepted this phone (an admin entered its activation code). */
    var activated: Boolean
        get() = sp.getBoolean("activated", false)
        set(v) = sp.edit().putBoolean("activated", v).apply()

    var newPerDay: Int
        get() = sp.getInt("new_per_day", 10)
        set(v) = sp.edit().putInt("new_per_day", v).apply()

    var autoPlayWords: Boolean
        get() = sp.getBoolean("auto_play", true)
        set(v) = sp.edit().putBoolean("auto_play", v).apply()

    fun forgetServer() {
        sp.edit().remove("token").remove("ws_url").putBoolean("activated", false).apply()
    }

    companion object {
        /** "e-learning.zenix.vn/" -> "https://e-learning.zenix.vn" */
        fun normalize(input: String): String {
            var s = input.trim().trimEnd('/')
            if (s.isEmpty()) return BuildConfig.DEFAULT_SERVER
            if (!s.startsWith("http://") && !s.startsWith("https://")) s = "https://$s"
            return s
        }
    }
}
