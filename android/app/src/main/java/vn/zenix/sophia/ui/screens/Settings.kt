package vn.zenix.sophia.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Logout
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import vn.zenix.sophia.ui.SettingsUi
import vn.zenix.sophia.ui.SettingsViewModel
import vn.zenix.sophia.ui.components.BigButton
import vn.zenix.sophia.ui.components.ProgressBar
import vn.zenix.sophia.ui.components.SoftCard
import vn.zenix.sophia.ui.components.TopBar
import vn.zenix.sophia.ui.theme.Palette

@Composable
fun SettingsScreen(vm: SettingsViewModel, onBack: () -> Unit, onUnlinked: () -> Unit) {
    val ui by vm.ui.collectAsState()
    SettingsContent(ui, onBack, vm::setNewPerDay, vm::setAutoPlay, vm::sync, onUnlink = { done -> vm.unlink(done) }, onUnlinked = onUnlinked)
}

@Composable
fun SettingsContent(
    ui: SettingsUi, onBack: () -> Unit, onNewPerDay: (Int) -> Unit, onAutoPlay: (Boolean) -> Unit,
    onSync: () -> Unit, onUnlink: (done: () -> Unit) -> Unit, onUnlinked: () -> Unit = {},
) {
    var confirm by remember { mutableStateOf(false) }

    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Palette.Ink.copy(alpha = 0.08f), Palette.Bg, Palette.Bg), endY = 700f))) {
        Column(Modifier.fillMaxSize()) {
            TopBar("Cài đặt", onBack)
            Column(
                Modifier.fillMaxSize().verticalScroll(rememberScrollState()).navigationBarsPadding().padding(horizontal = 18.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                Section("Học tập") {
                    Text("Số từ mới mỗi ngày", style = MaterialTheme.typography.titleSmall, color = Palette.Ink)
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(5, 10, 20, 30).forEach { n ->
                            val on = ui.newPerDay == n
                            Box(
                                Modifier.weight(1f).height(44.dp).clip(RoundedCornerShape(14.dp))
                                    .background(if (on) Palette.Blue else Palette.Soft).clickable { onNewPerDay(n) },
                                contentAlignment = Alignment.Center,
                            ) { Text("$n", color = if (on) Color.White else Palette.Ink, style = MaterialTheme.typography.titleMedium) }
                        }
                    }
                    Spacer(Modifier.height(14.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text("Tự đọc từ khi hiện thẻ", style = MaterialTheme.typography.titleSmall, color = Palette.Ink)
                            Text("Phát âm thanh ngay khi sang thẻ mới", style = MaterialTheme.typography.bodySmall, color = Palette.Muted)
                        }
                        Switch(
                            checked = ui.autoPlay, onCheckedChange = onAutoPlay,
                            colors = SwitchDefaults.colors(checkedTrackColor = Palette.Blue),
                        )
                    }
                }

                Section("Đồng bộ bộ từ") {
                    Text(
                        "Tải bộ từ mới, nhận các từ đã học với Sophia và gửi kết quả ôn của điện thoại lên máy chủ.",
                        style = MaterialTheme.typography.bodyMedium, color = Palette.Muted,
                    )
                    Spacer(Modifier.height(12.dp))
                    if (ui.syncing) {
                        Text(ui.syncText, style = MaterialTheme.typography.titleSmall, color = Palette.Ink)
                        Spacer(Modifier.height(8.dp))
                        ProgressBar((ui.syncPercent ?: 0) / 100f, Palette.Blue)
                        if (ui.syncPercent != null) Text("${ui.syncPercent}%", style = MaterialTheme.typography.labelMedium, color = Palette.Blue)
                    } else {
                        BigButton("Đồng bộ ngay", onSync, Modifier.fillMaxWidth(), Palette.Blue, Icons.Rounded.CloudDownload)
                        if (ui.syncResult.isNotEmpty()) {
                            Spacer(Modifier.height(10.dp))
                            Text(ui.syncResult, style = MaterialTheme.typography.bodyMedium, color = if (ui.syncOk) Palette.Green else Palette.Red)
                        }
                    }
                }

                Section("Thông tin thiết bị") {
                    InfoRow("Phiên bản", ui.version)
                    InfoRow("Mã thiết bị", ui.deviceId)
                    InfoRow("Máy chủ", ui.serverUrl.removePrefix("https://").removePrefix("http://"))
                    InfoRow("Bộ từ", if (ui.words > 0) "${ui.words} từ" else "Chưa tải")
                    if (ui.packId.isNotEmpty()) InfoRow("Mã bộ từ", ui.packId)
                    InfoRow("Lượt ôn chưa gửi", "${ui.pending}", last = true)
                }

                Section("Máy chủ") {
                    Text(
                        "Ngắt kết nối sẽ xóa bộ từ và tiến độ học offline trên điện thoại này. Điện thoại sẽ đăng ký lại như một thiết bị mới.",
                        style = MaterialTheme.typography.bodyMedium, color = Palette.Muted,
                    )
                    Spacer(Modifier.height(12.dp))
                    BigButton("Ngắt kết nối", { confirm = true }, Modifier.fillMaxWidth(), Palette.Red, Icons.AutoMirrored.Rounded.Logout)
                }
                Spacer(Modifier.height(18.dp))
            }
        }
    }

    if (confirm) {
        AlertDialog(
            onDismissRequest = { confirm = false },
            title = { Text("Ngắt kết nối?", style = MaterialTheme.typography.titleLarge) },
            text = { Text("Bộ từ và tiến độ học offline trên điện thoại sẽ bị xóa. Nên đồng bộ trước để không mất kết quả ôn.", style = MaterialTheme.typography.bodyMedium) },
            confirmButton = {
                TextButton(onClick = { confirm = false; onUnlink(onUnlinked) }) { Text("Ngắt kết nối", color = Palette.Red, style = MaterialTheme.typography.labelLarge) }
            },
            dismissButton = { TextButton(onClick = { confirm = false }) { Text("Giữ lại", color = Palette.Muted, style = MaterialTheme.typography.labelLarge) } },
            containerColor = Color.White,
        )
    }
}

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    Column {
        Text(title, style = MaterialTheme.typography.titleMedium, color = Palette.Muted, modifier = Modifier.padding(start = 6.dp, bottom = 8.dp))
        SoftCard(Modifier.fillMaxWidth()) { Column(Modifier.padding(18.dp)) { content() } }
    }
}

@Composable
private fun InfoRow(label: String, value: String, last: Boolean = false) {
    Column {
        Row(Modifier.fillMaxWidth().padding(vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.bodyMedium, color = Palette.Muted)
            Spacer(Modifier.width(12.dp))
            Text(value, style = MaterialTheme.typography.titleSmall, color = Palette.Ink, modifier = Modifier.weight(1f), maxLines = 2,
                textAlign = androidx.compose.ui.text.style.TextAlign.End)
        }
        if (!last) Box(Modifier.fillMaxWidth().height(1.5.dp).background(Palette.Soft))
    }
}
