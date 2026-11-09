package vn.zenix.sophia.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import vn.zenix.sophia.AppContainer
import vn.zenix.sophia.BuildConfig
import vn.zenix.sophia.SophiaApp
import vn.zenix.sophia.data.ApiException
import vn.zenix.sophia.data.Card
import vn.zenix.sophia.data.CardQueue
import vn.zenix.sophia.data.DAY_MINUTES
import vn.zenix.sophia.data.Focus
import vn.zenix.sophia.data.PickKind
import vn.zenix.sophia.data.Quality
import vn.zenix.sophia.data.QueueStats
import vn.zenix.sophia.data.Registration
import vn.zenix.sophia.data.formatInterval

private val AndroidViewModel.container: AppContainer get() = getApplication<SophiaApp>().container
private fun nowSec() = System.currentTimeMillis() / 1000

// ===================================================================== connecting this phone to the server
sealed interface GateState {
    data object Idle : GateState
    data object Connecting : GateState
    data class Pending(val code: String) : GateState
    data class Failed(val message: String) : GateState
    data object Ready : GateState
}

class SessionViewModel(app: Application) : AndroidViewModel(app) {
    private val prefs = container.prefs
    private val api = container.api
    private val _state = MutableStateFlow<GateState>(if (prefs.activated) GateState.Ready else GateState.Idle)
    val state: StateFlow<GateState> = _state.asStateFlow()
    val serverUrl get() = prefs.serverUrl
    private var poll: Job? = null

    fun connect(url: String) {
        prefs.serverUrl = url
        poll?.cancel()
        _state.value = GateState.Connecting
        viewModelScope.launch {
            try {
                handle(api.register())
            } catch (e: ApiException) {
                _state.value = GateState.Failed(e.message ?: "Không kết nối được")
            }
        }
    }

    private fun handle(r: Registration) {
        when (r) {
            is Registration.Ready -> {
                prefs.wsUrl = r.wsUrl; prefs.token = r.token; prefs.activated = true
                _state.value = GateState.Ready
            }
            is Registration.Pending -> {
                prefs.wsUrl = r.wsUrl; prefs.token = r.token; prefs.activated = false
                _state.value = GateState.Pending(r.code)
                startPolling()
            }
            is Registration.Blocked -> _state.value = GateState.Failed(r.message.ifBlank { "Thiết bị đã bị khóa" })
        }
    }

    /** While the admin has not typed the code yet: ask every few seconds. */
    private fun startPolling() {
        poll?.cancel()
        poll = viewModelScope.launch {
            while (true) {
                delay(3000)
                try {
                    if (api.isActivated()) {
                        handle(api.register())
                        return@launch
                    }
                } catch (e: ApiException) {
                    if (e.code == 403) {
                        _state.value = GateState.Failed(e.message ?: "Thiết bị đã bị khóa")
                        return@launch
                    }                                           // a dropped connection: try again
                }
            }
        }
    }

    fun back() {
        poll?.cancel()
        _state.value = GateState.Idle
    }
}

// ===================================================================== home
data class HomeUi(
    val hasPack: Boolean = false,
    val due: Int = 0,
    val newLeft: Int = 0,
    val streak: Int = 0,
    val syncing: Boolean = false,
    val syncText: String = "",
    val syncPercent: Int? = null,
    val syncError: String = "",
    val needsLogin: Boolean = false,
)

class HomeViewModel(app: Application) : AndroidViewModel(app) {
    private val library = container.library
    private val _ui = MutableStateFlow(HomeUi())
    val ui: StateFlow<HomeUi> = _ui.asStateFlow()

    init {
        viewModelScope.launch {
            library.load()
            publish()
            checkRegistration()
            if (!container.autoSynced) {
                container.autoSynced = true
                sync()
            }
        }
    }

    /** The numbers on the tiles; call again when the learner comes back from a study screen. */
    fun refresh() {
        viewModelScope.launch {
            if (!library.loaded) library.load()
            publish()
        }
    }

    private fun publish() {
        val p = library.progress
        val s = library.stats()
        _ui.update {
            it.copy(hasPack = library.loaded, due = s.due + s.learning, newLeft = s.newLeft, streak = p?.streak(nowSec()) ?: 0)
        }
    }

    private suspend fun checkRegistration() {
        try {
            when (val r = container.api.register()) {
                is Registration.Ready -> { container.prefs.wsUrl = r.wsUrl; container.prefs.token = r.token }
                is Registration.Pending, is Registration.Blocked -> {
                    container.prefs.activated = false
                    _ui.update { it.copy(needsLogin = true) }
                }
            }
        } catch (e: ApiException) {
            // offline: keep working with what is on the phone
        }
    }

    fun sync() {
        if (_ui.value.syncing) return
        _ui.update { it.copy(syncing = true, syncError = "", syncText = "Đang kết nối…", syncPercent = null) }
        viewModelScope.launch {
            val result = library.sync { text, pct -> _ui.update { it.copy(syncText = text, syncPercent = pct) } }
            publish()
            _ui.update { it.copy(syncing = false, syncError = if (result.ok) "" else result.message) }
        }
    }
}

// ===================================================================== flashcards
data class StudyUi(
    val loading: Boolean = true,
    val noPack: Boolean = false,
    val index: Int = -1,
    val card: Card? = null,
    val kind: PickKind = PickKind.None,
    val revealed: Boolean = false,
    val stats: QueueStats = QueueStats(),
    val answered: Int = 0,
    val finished: Boolean = false,
    val toast: String = "",
    val toastGood: Boolean = true,
    val hasAudio: Boolean = false,
)

class StudyViewModel(app: Application) : AndroidViewModel(app) {
    private val library = container.library
    private val prefs = container.prefs
    private val audio = container.wordAudio
    private val _ui = MutableStateFlow(StudyUi())
    val ui: StateFlow<StudyUi> = _ui.asStateFlow()
    private var focus = Focus.New
    private var extra = false
    private var started = false
    private var toastJob: Job? = null

    fun start(focus: Focus) {
        if (started) return
        started = true
        this.focus = focus
        viewModelScope.launch {
            if (!library.loaded) library.load()
            if (!library.loaded) _ui.update { it.copy(loading = false, noPack = true) } else showNext(-1)
        }
    }

    private fun showNext(avoid: Int) {
        val p = library.progress ?: return
        val pack = library.pack ?: return
        val now = nowSec()
        val pick = CardQueue.next(p.cards, now, p.newToday(now), prefs.newPerDay, avoid, focus, extra)
        val stats = library.stats()
        if (!pick.found) {
            audio.stop()
            _ui.update { it.copy(loading = false, noPack = false, finished = true, card = null, stats = stats) }
            return
        }
        _ui.update {
            it.copy(loading = false, finished = false, index = pick.index, card = pack.card(pick.index), kind = pick.kind,
                revealed = false, stats = stats, hasAudio = pack.hasAudio(pick.index))
        }
        if (prefs.autoPlayWords) speak()
    }

    fun speak() {
        val pack = library.pack ?: return
        val i = _ui.value.index
        if (i < 0) return
        viewModelScope.launch { pack.audio(i)?.let { audio.play(it) } }
    }

    fun reveal() {
        if (_ui.value.card != null) _ui.update { it.copy(revealed = true) }
    }

    fun answer(quality: Int) {
        val p = library.progress ?: return
        val i = _ui.value.index
        if (i < 0) return
        val minutes = p.record(i, quality, nowSec())
        viewModelScope.launch { library.save() }
        val label = when {
            quality == Quality.FORGOT -> "Quên"
            quality >= Quality.EASY -> "Dễ"
            else -> "Nhớ"
        }
        toast("$label · ${formatInterval(minutes)}", quality != Quality.FORGOT)
        _ui.update { it.copy(answered = it.answered + 1) }
        showNext(i)
    }

    /** "Học thêm" / "Ôn thêm" after the day's goal is done. */
    fun more() {
        extra = true
        showNext(-1)
    }

    private fun toast(text: String, good: Boolean) {
        toastJob?.cancel()
        _ui.update { it.copy(toast = text, toastGood = good) }
        toastJob = viewModelScope.launch {
            delay(1800)
            _ui.update { it.copy(toast = "") }
        }
    }

    override fun onCleared() = audio.stop()
}

// ===================================================================== dictionary
data class DictEntry(val word: String, val index: Int)

class DictionaryViewModel(app: Application) : AndroidViewModel(app) {
    private val library = container.library
    private val audio = container.wordAudio
    private val _entries = MutableStateFlow<List<DictEntry>>(emptyList())
    val entries: StateFlow<List<DictEntry>> = _entries.asStateFlow()
    private val _loading = MutableStateFlow(true)
    val loading: StateFlow<Boolean> = _loading.asStateFlow()
    val hasPack get() = library.loaded

    init {
        viewModelScope.launch {
            if (!library.loaded) library.load()
            _entries.value = library.words().mapIndexed { i, w -> DictEntry(w, i) }
                .filter { it.word.isNotEmpty() }.sortedBy { it.word.lowercase() }
            _loading.value = false
        }
    }

    fun card(index: Int): Card? = library.pack?.card(index)
    fun hasAudio(index: Int) = library.pack?.hasAudio(index) == true
    fun speak(index: Int) { library.pack?.audio(index)?.let { audio.play(it) } }
    fun stopAudio() = audio.stop()
    override fun onCleared() = audio.stop()
}

// ===================================================================== achievements
data class StatsUi(
    val loaded: Boolean = false,
    val total: Int = 0,
    val seen: Int = 0,
    val mastered: Int = 0,        // interval of three weeks or more
    val young: Int = 0,           // seen, between one day and three weeks
    val learning: Int = 0,        // seen, still in the short steps
    val due: Int = 0,
    val newToday: Int = 0,
    val streak: Int = 0,
    val reviews: Int = 0,
    val accuracy: Int = 0,
)

class StatsViewModel(app: Application) : AndroidViewModel(app) {
    private val library = container.library
    private val _ui = MutableStateFlow(StatsUi())
    val ui: StateFlow<StatsUi> = _ui.asStateFlow()

    init {
        viewModelScope.launch {
            if (!library.loaded) library.load()
            val p = library.progress ?: return@launch
            val now = nowSec()
            var mastered = 0; var young = 0; var learning = 0
            for (c in p.cards) {
                if (!c.seen) continue
                when {
                    c.intervalMin >= 21 * DAY_MINUTES -> mastered++
                    c.intervalMin >= DAY_MINUTES -> young++
                    else -> learning++
                }
            }
            val s = library.stats()
            _ui.value = StatsUi(
                loaded = true, total = p.count, seen = mastered + young + learning, mastered = mastered, young = young,
                learning = learning, due = s.due + s.learning, newToday = p.newToday(now), streak = p.streak(now),
                reviews = p.reviews, accuracy = if (p.reviews == 0) 0 else p.correct * 100 / p.reviews,
            )
        }
    }
}

// ===================================================================== settings
data class SettingsUi(
    val serverUrl: String = "",
    val newPerDay: Int = 10,
    val autoPlay: Boolean = true,
    val deviceId: String = "",
    val version: String = BuildConfig.VERSION_NAME,
    val packId: String = "",
    val words: Int = 0,
    val pending: Int = 0,
    val syncing: Boolean = false,
    val syncText: String = "",
    val syncPercent: Int? = null,
    val syncResult: String = "",
    val syncOk: Boolean = true,
)

class SettingsViewModel(app: Application) : AndroidViewModel(app) {
    private val prefs = container.prefs
    private val library = container.library
    private val _ui = MutableStateFlow(SettingsUi())
    val ui: StateFlow<SettingsUi> = _ui.asStateFlow()

    init {
        viewModelScope.launch {
            if (!library.loaded) library.load()
            publish()
        }
    }

    private fun publish() {
        _ui.update {
            it.copy(
                serverUrl = prefs.serverUrl, newPerDay = prefs.newPerDay, autoPlay = prefs.autoPlayWords, deviceId = prefs.deviceId,
                packId = library.installedPackId().orEmpty(), words = library.pack?.count ?: 0,
                pending = library.progress?.pendingEvents?.size ?: 0,
            )
        }
    }

    fun setNewPerDay(n: Int) { prefs.newPerDay = n; publish() }
    fun setAutoPlay(on: Boolean) { prefs.autoPlayWords = on; publish() }

    fun sync() {
        if (_ui.value.syncing) return
        _ui.update { it.copy(syncing = true, syncResult = "", syncText = "Đang kết nối…", syncPercent = null) }
        viewModelScope.launch {
            val result = library.sync { text, pct -> _ui.update { it.copy(syncText = text, syncPercent = pct) } }
            publish()
            _ui.update { it.copy(syncing = false, syncResult = result.message, syncOk = result.ok) }
        }
    }

    /** Forget this phone's link with the server (it registers again, as a new device, the next time). */
    fun unlink(done: () -> Unit) {
        viewModelScope.launch {
            library.wipe()
            prefs.forgetServer()
            container.autoSynced = false
            done()
        }
    }
}

// ===================================================================== conversation
class VoiceViewModel(app: Application) : AndroidViewModel(app) {
    private val client = container.newVoice()
    val state = client.state
    private var started = false

    fun start(mode: VoiceMode) {
        if (started) return
        started = true
        client.start(mode.openText)
    }

    fun toggle() = client.toggleTalk()
    fun retry() = client.retry()
    fun stop() = client.stop()
    override fun onCleared() = client.stop()
}
