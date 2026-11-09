package vn.zenix.sophia.ui.screens

import androidx.compose.foundation.background
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.LocalFireDepartment
import androidx.compose.material.icons.rounded.MenuBook
import androidx.compose.material.icons.rounded.Replay
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import vn.zenix.sophia.ui.StatsUi
import vn.zenix.sophia.ui.StatsViewModel
import vn.zenix.sophia.ui.components.CenterMessage
import vn.zenix.sophia.ui.components.SoftCard
import vn.zenix.sophia.ui.components.SophiaFrame
import vn.zenix.sophia.ui.components.SophiaImage
import vn.zenix.sophia.ui.components.TopBar
import vn.zenix.sophia.ui.theme.Palette

@Composable
fun StatsScreen(vm: StatsViewModel, onBack: () -> Unit) {
    val ui by vm.ui.collectAsState()
    StatsContent(ui, onBack)
}

@Composable
fun StatsContent(ui: StatsUi, onBack: () -> Unit) {
    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Palette.Green.copy(alpha = 0.16f), Palette.Bg, Palette.Bg), endY = 900f))) {
        Column(Modifier.fillMaxSize()) {
            TopBar("Thành tích", onBack)
            if (!ui.loaded) {
                CenterMessage("Chưa có số liệu", "Hãy tải bộ từ và học vài từ, thành tích sẽ hiện ở đây.")
                return@Column
            }
            Column(
                Modifier.fillMaxSize().verticalScroll(rememberScrollState()).navigationBarsPadding().padding(horizontal = 18.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                SoftCard(Modifier.fillMaxWidth()) {
                    Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                        SophiaImage(SophiaFrame.Happy, 96.dp)
                        Spacer(Modifier.width(10.dp))
                        Column {
                            Text(
                                if (ui.streak > 0) "${ui.streak} ngày liên tiếp!" else "Bắt đầu chuỗi ngày học nào!",
                                style = MaterialTheme.typography.titleLarge, color = Palette.Ink,
                            )
                            Text(
                                if (ui.seen == 0) "Học từ đầu tiên hôm nay nhé." else "Bạn đã học ${ui.seen} trên ${ui.total} từ.",
                                style = MaterialTheme.typography.bodyMedium, color = Palette.Muted,
                            )
                        }
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                    Stat("Chuỗi ngày", "${ui.streak}", Icons.Rounded.LocalFireDepartment, Palette.Orange, Modifier.weight(1f))
                    Stat("Từ đã học", "${ui.seen}", Icons.Rounded.MenuBook, Palette.Blue, Modifier.weight(1f))
                }
                Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                    Stat("Cần ôn", "${ui.due}", Icons.Rounded.Replay, Palette.Purple, Modifier.weight(1f))
                    Stat("Trả lời đúng", "${ui.accuracy}%", Icons.Rounded.CheckCircle, Palette.Green, Modifier.weight(1f))
                }
                SoftCard(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(18.dp)) {
                        Text("Tiến độ bộ từ", style = MaterialTheme.typography.titleMedium, color = Palette.Ink)
                        Spacer(Modifier.height(12.dp))
                        Distribution(ui.mastered, ui.young, ui.learning, ui.total - ui.seen)
                        Spacer(Modifier.height(14.dp))
                        Legend("Thuộc lòng", ui.mastered, Palette.Green)
                        Legend("Đang nhớ", ui.young, Palette.Blue)
                        Legend("Đang học", ui.learning, Palette.Amber)
                        Legend("Chưa học", ui.total - ui.seen, Palette.Rim)
                    }
                }
                Text(
                    "Đã trả lời ${ui.reviews} thẻ · hôm nay học ${ui.newToday} từ mới",
                    style = MaterialTheme.typography.bodySmall, color = Palette.Muted, modifier = Modifier.padding(horizontal = 6.dp),
                )
                Spacer(Modifier.height(18.dp))
            }
        }
    }
}

@Composable
private fun Stat(label: String, value: String, icon: ImageVector, color: Color, modifier: Modifier) {
    SoftCard(modifier) {
        Column(Modifier.padding(16.dp)) {
            Box(Modifier.size(40.dp).clip(CircleShape).background(color.copy(alpha = 0.14f)), contentAlignment = Alignment.Center) {
                Icon(icon, null, tint = color, modifier = Modifier.size(24.dp))
            }
            Spacer(Modifier.height(10.dp))
            Text(value, style = MaterialTheme.typography.headlineLarge, color = Palette.Ink)
            Text(label, style = MaterialTheme.typography.bodySmall, color = Palette.Muted)
        }
    }
}

/** One bar split into the groups of the pack. */
@Composable
private fun Distribution(mastered: Int, young: Int, learning: Int, fresh: Int) {
    val parts = listOf(mastered to Palette.Green, young to Palette.Blue, learning to Palette.Amber, fresh to Palette.Rim).filter { it.first > 0 }
    Row(Modifier.fillMaxWidth().height(18.dp).clip(RoundedCornerShape(50))) {
        parts.forEach { (n, color) -> Box(Modifier.weight(n.toFloat()).fillMaxSize().background(color)) }
    }
}

@Composable
private fun Legend(label: String, n: Int, color: Color) {
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(12.dp).clip(CircleShape).background(color))
        Spacer(Modifier.width(10.dp))
        Text(label, style = MaterialTheme.typography.bodyMedium, color = Palette.Ink, modifier = Modifier.weight(1f))
        Text("$n", style = MaterialTheme.typography.titleSmall, color = Palette.Ink)
    }
}
