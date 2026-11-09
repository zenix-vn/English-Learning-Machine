package vn.zenix.sophia

import android.app.Application
import android.content.Context
import vn.zenix.sophia.data.Api
import vn.zenix.sophia.data.Library
import vn.zenix.sophia.data.Prefs
import vn.zenix.sophia.data.WordAudio
import vn.zenix.sophia.voice.VoiceClient

/** The few objects the whole app shares. */
class AppContainer(context: Context) {
    val prefs = Prefs(context)
    val api = Api(prefs)
    val library = Library(context, api, prefs)
    val wordAudio = WordAudio(context)

    /** The pack is checked against the server once per launch of the app. */
    var autoSynced = false

    fun newVoice() = VoiceClient(api.http, prefs)
}

class SophiaApp : Application() {
    val container by lazy { AppContainer(this) }
}
