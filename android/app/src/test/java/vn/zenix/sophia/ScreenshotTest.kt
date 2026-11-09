package vn.zenix.sophia

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import app.cash.paparazzi.DeviceConfig
import app.cash.paparazzi.Paparazzi
import com.android.ide.common.rendering.api.SessionParams
import org.junit.Rule
import org.junit.Test
import vn.zenix.sophia.data.Card
import vn.zenix.sophia.data.PickKind
import vn.zenix.sophia.data.QueueStats
import vn.zenix.sophia.ui.HomeUi
import vn.zenix.sophia.ui.SettingsUi
import vn.zenix.sophia.ui.StatsUi
import vn.zenix.sophia.ui.StudyUi
import vn.zenix.sophia.ui.VoiceMode
import vn.zenix.sophia.ui.screens.Cards
import vn.zenix.sophia.ui.screens.Conversation
import vn.zenix.sophia.ui.screens.HomeContent
import vn.zenix.sophia.ui.screens.SettingsContent
import vn.zenix.sophia.ui.screens.StatsContent
import vn.zenix.sophia.ui.theme.Palette
import vn.zenix.sophia.ui.theme.SophiaTheme
import vn.zenix.sophia.voice.Phase
import vn.zenix.sophia.voice.VoiceUi

/** Renders the screens to PNG files (app/build/reports/paparazzi) so the layout can be checked without a phone. */
class ScreenshotTest {
    @get:Rule
    val paparazzi = Paparazzi(deviceConfig = DeviceConfig.PIXEL_5, theme = "android:Theme.Material.Light.NoActionBar")

    private fun shot(name: String, content: @androidx.compose.runtime.Composable () -> Unit) {
        paparazzi.snapshot(name) { SophiaTheme { Box(Modifier.fillMaxSize().background(Palette.Bg)) { content() } } }
    }

    @Test fun home() = shot("home") {
        HomeContent(HomeUi(hasPack = true, due = 12, newLeft = 10, streak = 4), {}, {}, {}, {}, {}, {})
    }

    @Test fun homeNoPack() = shot("home_nopack") {
        HomeContent(HomeUi(hasPack = false), {}, {}, {}, {}, {}, {})
    }

    @Test fun voiceSpeaking() = shot("voice_speaking") {
        Conversation(
            VoiceMode.Learn,
            VoiceUi(
                Phase.Speaking, "Mình sẵn sàng rồi",
                "Chào bạn! Bài học hôm nay là Chủ đề 1, bài 2: Giới thiệu bản thân. Bạn đã học xong 1 trên 24 bài. Bạn sẵn sàng bắt đầu chưa?",
            ),
        ) {}
    }

    @Test fun voiceListening() = shot("voice_listening") {
        Conversation(VoiceMode.Translate, VoiceUi(Phase.Listening), {})
    }

    private val card = Card("serendipity", "/ˌser.ənˈdɪp.ə.ti/", "sự tình cờ may mắn; khả năng tìm ra điều hay một cách tình cờ", "A happy serendipity brought us together. — Một sự tình cờ may mắn đã đưa chúng tôi đến với nhau.")
    private val study = StudyUi(loading = false, index = 4, card = card, kind = PickKind.New, stats = QueueStats(fresh = 90, newLeft = 7), answered = 3, hasAudio = true)

    @Test fun studyFront() = shot("study_front") { Cards(study, Palette.Blue, {}, {}, {}) }

    @Test fun studyBack() = shot("study_back") { Cards(study.copy(revealed = true, toast = "Nhớ · gặp lại sau 1 ngày"), Palette.Blue, {}, {}, {}) }

    @Test fun stats() = shot("stats") {
        StatsContent(StatsUi(loaded = true, total = 2000, seen = 340, mastered = 60, young = 190, learning = 90, due = 23, newToday = 6, streak = 7, reviews = 812, accuracy = 86), {})
    }

    @Test fun settings() = shot("settings") {
        SettingsContent(
            SettingsUi(serverUrl = "https://e-learning.zenix.vn", newPerDay = 10, deviceId = "android-8f3a91c2d4b70e15", packId = "d7-2000-85d09db1c0", words = 2000, pending = 5),
            {}, {}, {}, {}, {},
        )
    }
}
