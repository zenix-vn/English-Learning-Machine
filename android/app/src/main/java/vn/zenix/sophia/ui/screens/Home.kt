package vn.zenix.sophia.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.EmojiEvents
import androidx.compose.material.icons.rounded.LocalFireDepartment
import androidx.compose.material.icons.rounded.MenuBook
import androidx.compose.material.icons.rounded.Replay
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import java.util.Calendar
import vn.zenix.sophia.ui.HomeUi
import vn.zenix.sophia.ui.HomeViewModel
import vn.zenix.sophia.ui.VoiceMode
import vn.zenix.sophia.ui.components.AnimatedSophia
import vn.zenix.sophia.ui.components.BigButton
import vn.zenix.sophia.ui.components.Pill
import vn.zenix.sophia.ui.components.ProgressBar
import vn.zenix.sophia.ui.components.SoftCard
import vn.zenix.sophia.ui.components.TileCard
import vn.zenix.sophia.ui.theme.Palette
import vn.zenix.sophia.voice.Phase

private fun greeting(): String {
    val h = Calendar.getInstance().get(Calendar.HOUR_OF_DAY)
    return when {
        h < 11 -> "Chào buổi sáng!"
        h < 18 -> "Chào buổi chiều!"
        else -> "Chào buổi tối!"
    }
}

private class Tile(val title: String, val icon: ImageVector, val color: Color, val light: Color, val badge: Int?, val go: () -> Unit)

@Composable
fun HomeScreen(
    vm: HomeViewModel,
    onStudy: (String) -> Unit,
    onVoice: (VoiceMode) -> Unit,
    onDictionary: () -> Unit,
    onStats: () -> Unit,
    onSettings: () -> Unit,
    onNeedsLogin: () -> Unit,
) {
    val ui by vm.ui.collectAsState()
    LaunchedEffect(Unit) { vm.refresh() }
    LaunchedEffect(ui.needsLogin) { if (ui.needsLogin) onNeedsLogin() }
    HomeContent(ui, vm::sync, onStudy, onVoice, onDictionary, onStats, onSettings)
}

@Composable
fun HomeContent(
    ui: HomeUi,
    onSync: () -> Unit,
    onStudy: (String) -> Unit,
    onVoice: (VoiceMode) -> Unit,
    onDictionary: () -> Unit,
    onStats: () -> Unit,
    onSettings: () -> Unit,
) {
    val tiles = listOf(
        Tile("Học từ mới", Icons.Rounded.MenuBook, Palette.Blue, Palette.BlueLight, ui.newLeft) { onStudy("new") },
        Tile("Ôn tập", Icons.Rounded.Replay, Palette.Orange, Palette.OrangeLight, ui.due) { onStudy("review") },
        Tile("Từ điển", Icons.Rounded.Search, Palette.Teal, Palette.TealLight, null) { onDictionary() },
        Tile(VoiceMode.Learn.title, VoiceMode.Learn.icon, VoiceMode.Learn.color, VoiceMode.Learn.light, null) { onVoice(VoiceMode.Learn) },
        Tile(VoiceMode.Pronounce.title, VoiceMode.Pronounce.icon, VoiceMode.Pronounce.color, VoiceMode.Pronounce.light, null) { onVoice(VoiceMode.Pronounce) },
        Tile("Thành tích", Icons.Rounded.EmojiEvents, Palette.Green, Palette.GreenLight, null) { onStats() },
        Tile(VoiceMode.Translate.title, VoiceMode.Translate.icon, VoiceMode.Translate.color, VoiceMode.Translate.light, null) { onVoice(VoiceMode.Translate) },
        Tile(VoiceMode.Chat.title, VoiceMode.Chat.icon, VoiceMode.Chat.color, VoiceMode.Chat.light, null) { onVoice(VoiceMode.Chat) },
    )

    Box(
        Modifier.fillMaxSize()
            .background(Brush.verticalGradient(listOf(Palette.PurpleLight.copy(alpha = 0.22f), Palette.Bg, Palette.Bg), endY = 900f)),
    ) {
        LazyVerticalGrid(
            columns = GridCells.Fixed(2),
            modifier = Modifier.fillMaxSize().statusBarsPadding(),
            contentPadding = PaddingValues(start = 18.dp, end = 18.dp, top = 8.dp, bottom = 28.dp),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            item(span = { GridItemSpan(2) }) { Header(ui.streak, onSettings) }
            item(span = { GridItemSpan(2) }) { SophiaCard(ui.hasPack, ui.due, ui.newLeft) }
            item(span = { GridItemSpan(2) }) {
                AnimatedVisibility(
                    visible = !ui.hasPack || ui.syncing || ui.syncError.isNotEmpty(),
                    enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically(),
                ) { SyncBanner(ui.hasPack, ui.syncing, ui.syncText, ui.syncPercent, ui.syncError, onSync) }
            }
            tiles.forEach { t ->
                item { TileCard(t.title, t.icon, t.color, t.light, Modifier.fillMaxWidth(), t.badge, t.go) }
            }
            item(span = { GridItemSpan(2) }) { Spacer(Modifier.navigationBarsPadding()) }
        }
    }
}

@Composable
private fun Header(streak: Int, onSettings: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text("Sophia", style = MaterialTheme.typography.headlineLarge, color = Palette.Ink)
            Text("Học tiếng Anh mỗi ngày", style = MaterialTheme.typography.bodyMedium, color = Palette.Muted)
        }
        if (streak > 0) {
            Pill("$streak ngày", Palette.Orange, icon = Icons.Rounded.LocalFireDepartment, background = Palette.AmberSoft)
            Spacer(Modifier.width(10.dp))
        }
        Box(
            Modifier.size(46.dp).shadow(4.dp, CircleShape, ambientColor = Palette.Ink.copy(alpha = 0.1f))
                .clip(CircleShape).background(Color.White).clickable(onClick = onSettings),
            contentAlignment = Alignment.Center,
        ) { Icon(Icons.Rounded.Settings, "Cài đặt", tint = Palette.Ink) }
    }
}

/** Sophia and what she says about today, like the speech bubble on the device. */
@Composable
private fun SophiaCard(hasPack: Boolean, due: Int, newLeft: Int) {
    SoftCard(Modifier.fillMaxWidth()) {
        Row(Modifier.padding(start = 6.dp, end = 16.dp, top = 12.dp, bottom = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            AnimatedSophia(Phase.Idle, 104.dp)
            Spacer(Modifier.width(4.dp))
            Column(Modifier.weight(1f)) {
                Text(greeting(), style = MaterialTheme.typography.titleLarge, color = Palette.Ink)
                val line = when {
                    !hasPack -> "Mình tải bộ từ về để học nhé!"
                    due > 0 -> "Hôm nay có $due từ cần ôn."
                    newLeft > 0 -> "Hôm nay học $newLeft từ mới nào!"
                    else -> "Hôm nay mình học gì nào?"
                }
                Text(line, style = MaterialTheme.typography.bodyMedium, color = Palette.Muted)
                if (hasPack && (due > 0 || newLeft > 0)) {
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        if (due > 0) Pill("$due ôn", Palette.Orange)
                        if (newLeft > 0) Pill("$newLeft mới", Palette.Blue)
                    }
                }
            }
        }
    }
}

@Composable
private fun SyncBanner(hasPack: Boolean, syncing: Boolean, text: String, percent: Int?, error: String, onSync: () -> Unit) {
    SoftCard(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            when {
                syncing -> {
                    Text(text, style = MaterialTheme.typography.titleSmall, color = Palette.Ink)
                    Spacer(Modifier.height(10.dp))
                    ProgressBar((percent ?: 0) / 100f, Palette.Blue)
                    if (percent != null) {
                        Spacer(Modifier.height(4.dp))
                        Text("$percent%", style = MaterialTheme.typography.labelMedium, color = Palette.Blue)
                    }
                }
                error.isNotEmpty() -> {
                    Text(error, style = MaterialTheme.typography.bodyMedium, color = Palette.Red)
                    Spacer(Modifier.height(10.dp))
                    BigButton("Thử lại", onSync, Modifier.fillMaxWidth(), Palette.Blue, Icons.Rounded.CloudDownload)
                }
                !hasPack -> {
                    Text("Chưa có bộ từ", style = MaterialTheme.typography.titleSmall, color = Palette.Ink)
                    Text("Tải bộ từ của bạn để học và ôn offline.", style = MaterialTheme.typography.bodyMedium, color = Palette.Muted)
                    Spacer(Modifier.height(10.dp))
                    BigButton("Tải bộ từ", onSync, Modifier.fillMaxWidth(), Palette.Blue, Icons.Rounded.CloudDownload)
                }
            }
        }
    }
}
