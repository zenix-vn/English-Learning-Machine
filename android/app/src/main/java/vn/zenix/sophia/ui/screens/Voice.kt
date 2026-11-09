package vn.zenix.sophia.ui.screens

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Mic
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import vn.zenix.sophia.ui.VoiceMode
import vn.zenix.sophia.ui.VoiceViewModel
import vn.zenix.sophia.ui.components.AnimatedSophia
import vn.zenix.sophia.ui.components.BigButton
import vn.zenix.sophia.ui.components.TopBar
import vn.zenix.sophia.ui.theme.Palette
import vn.zenix.sophia.voice.Phase
import vn.zenix.sophia.voice.VoiceUi

@Composable
fun VoiceScreen(mode: VoiceMode, vm: VoiceViewModel, onBack: () -> Unit) {
    val context = LocalContext.current
    val ui by vm.state.collectAsState()
    var allowed by remember {
        mutableStateOf(ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED)
    }
    var asked by remember { mutableStateOf(false) }
    val request = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { allowed = it; asked = true }

    LaunchedEffect(Unit) { if (!allowed) request.launch(Manifest.permission.RECORD_AUDIO) }
    LaunchedEffect(allowed) { if (allowed) vm.start(mode) }
    DisposableEffect(Unit) { onDispose { vm.stop() } }

    Box(
        Modifier.fillMaxSize()
            .background(Brush.verticalGradient(listOf(mode.light.copy(alpha = 0.30f), Palette.Bg, Palette.Bg), endY = 1100f)),
    ) {
        Column(Modifier.fillMaxSize()) {
            TopBar(mode.title, onBack)
            if (!allowed) {
                PermissionNeeded(mode, asked) { request.launch(Manifest.permission.RECORD_AUDIO) }
            } else {
                Conversation(mode, ui, vm::toggle)
            }
        }
    }
}

@Composable
private fun PermissionNeeded(mode: VoiceMode, asked: Boolean, onAsk: () -> Unit) {
    Column(Modifier.fillMaxSize().padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        AnimatedSophia(Phase.Idle, 160.dp)
        Spacer(Modifier.height(12.dp))
        Text("Sophia cần nghe bạn nói", style = MaterialTheme.typography.headlineSmall, color = Palette.Ink, textAlign = TextAlign.Center)
        Spacer(Modifier.height(6.dp))
        Text(
            if (asked) "Hãy cho phép dùng micro trong Cài đặt của điện thoại, hoặc bấm nút dưới đây." else "Cho phép dùng micro để trò chuyện.",
            style = MaterialTheme.typography.bodyMedium, color = Palette.Muted, textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(20.dp))
        BigButton("Cho phép micro", onAsk, Modifier.fillMaxWidth(), mode.color, Icons.Rounded.Mic)
    }
}

@Composable
fun Conversation(mode: VoiceMode, ui: VoiceUi, onTalk: () -> Unit) {
    Column(Modifier.fillMaxSize().navigationBarsPadding().padding(horizontal = 18.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        StatusPill(ui)
        Spacer(Modifier.height(8.dp))
        SophiaRing(ui.phase, mode.color)
        Spacer(Modifier.height(10.dp))

        // what the learner said, then Sophia's answer
        Column(Modifier.weight(1f).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            AnimatedVisibility(ui.userText.isNotEmpty(), enter = fadeIn() + slideInVertically { it / 2 }, exit = fadeOut()) {
                Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.CenterEnd) {
                    Text(
                        ui.userText, color = Color.White, style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.widthIn(max = 300.dp).clip(RoundedCornerShape(22.dp, 22.dp, 6.dp, 22.dp))
                            .background(mode.color).padding(horizontal = 16.dp, vertical = 10.dp),
                    )
                }
            }
            Box(
                Modifier.weight(1f).fillMaxWidth()
                    .shadow(6.dp, RoundedCornerShape(26.dp), ambientColor = Palette.Ink.copy(alpha = 0.08f))
                    .clip(RoundedCornerShape(26.dp)).background(Color.White).padding(18.dp),
            ) {
                val scroll = rememberScrollState()
                LaunchedEffect(ui.botText) { scroll.animateScrollTo(scroll.maxValue) }
                if (ui.botText.isEmpty()) {
                    Text(hintFor(mode, ui), style = MaterialTheme.typography.bodyLarge, color = Palette.Faint)
                } else {
                    Column(Modifier.verticalScroll(scroll)) {
                        Text(ui.botText, style = MaterialTheme.typography.headlineSmall.copy(fontSize = 21.sp, lineHeight = 29.sp), color = Palette.Ink)
                    }
                }
            }
        }

        if (ui.phase == Phase.Error) {
            Spacer(Modifier.height(8.dp))
            Text(ui.error, color = Palette.Red, style = MaterialTheme.typography.bodyMedium, textAlign = TextAlign.Center)
        }
        Spacer(Modifier.height(12.dp))
        TalkButton(ui.phase, mode.color, onClick = onTalk)
        Spacer(Modifier.height(6.dp))
        Text(buttonLabel(ui.phase), style = MaterialTheme.typography.labelLarge, color = Palette.Muted)
        Spacer(Modifier.height(14.dp))
    }
}

private fun hintFor(mode: VoiceMode, ui: VoiceUi): String = when (ui.phase) {
    Phase.Connecting -> "Đang kết nối với Sophia…"
    Phase.Thinking -> "Sophia đang chuẩn bị…"
    Phase.Speaking -> "Sophia đang chuẩn bị giọng nói…"
    Phase.Listening -> mode.tip
    Phase.Idle -> "Chạm vào nút micro để nói tiếp nhé!"
    else -> mode.tip
}

private fun buttonLabel(phase: Phase) = when (phase) {
    Phase.Listening -> "Chạm để dừng"
    Phase.Speaking -> "Chạm để ngắt lời"
    Phase.Idle -> "Chạm để nói"
    Phase.Error -> "Chạm để thử lại"
    else -> "Đợi một chút…"
}

@Composable
private fun StatusPill(ui: VoiceUi) {
    val (text, color) = when (ui.phase) {
        Phase.Connecting -> "Đang kết nối…" to Palette.Amber
        Phase.Thinking -> "Sophia đang suy nghĩ" to Palette.Blue
        Phase.Listening -> "Sophia đang nghe bạn" to Palette.Green
        Phase.Speaking -> "Sophia đang nói" to Palette.Purple
        Phase.Idle -> "Đã dừng" to Palette.Faint
        Phase.Error -> "Mất kết nối" to Palette.Red
    }
    val dot by animateColorAsState(color, tween(300), label = "dot")
    Row(
        Modifier.shadow(4.dp, RoundedCornerShape(50), ambientColor = Palette.Ink.copy(alpha = 0.1f))
            .clip(RoundedCornerShape(50)).background(Color.White).padding(horizontal = 18.dp, vertical = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(12.dp).clip(CircleShape).background(dot))
        Spacer(Modifier.size(10.dp))
        Text(text, style = MaterialTheme.typography.titleSmall, color = Palette.Ink)
    }
}

/** Sophia in a ring that pulses while someone is talking (the same idea as the device's chat screen). */
@Composable
private fun SophiaRing(phase: Phase, color: Color) {
    val ring = when (phase) {
        Phase.Listening -> Palette.Green
        Phase.Speaking -> Palette.Purple
        Phase.Thinking -> Palette.Blue
        Phase.Connecting -> Palette.Amber
        else -> Palette.Rim
    }
    val ringColor by animateColorAsState(ring, tween(300), label = "ring")
    val pulse = rememberInfiniteTransition(label = "pulse")
    val a by pulse.animateFloat(0.35f, 1f, infiniteRepeatable(tween(700), RepeatMode.Reverse), label = "a")
    val active = phase == Phase.Listening || phase == Phase.Speaking
    Box(
        Modifier.size(214.dp).clip(CircleShape).background(Color.White.copy(alpha = 0.7f))
            .border(7.dp, ringColor.copy(alpha = if (active) a else 1f), CircleShape),
        contentAlignment = Alignment.Center,
    ) { AnimatedSophia(phase, 176.dp, bounce = phase != Phase.Speaking) }
}

@Composable
private fun TalkButton(phase: Phase, color: Color, onClick: () -> Unit) {
    val enabled = phase != Phase.Connecting && phase != Phase.Thinking
    val tint = when (phase) {
        Phase.Listening -> Palette.Green
        Phase.Error -> Palette.Red
        else -> color
    }
    val pulse = rememberInfiniteTransition(label = "talk")
    val s by pulse.animateFloat(1f, 1.18f, infiniteRepeatable(tween(850), RepeatMode.Reverse), label = "s")
    Box(contentAlignment = Alignment.Center, modifier = Modifier.size(104.dp)) {
        if (phase == Phase.Listening) {
            Box(Modifier.size(84.dp).scale(s).clip(CircleShape).background(tint.copy(alpha = 0.18f)))
        }
        Box(
            Modifier.size(84.dp).alpha(if (enabled) 1f else 0.5f)
                .shadow(12.dp, CircleShape, ambientColor = tint.copy(alpha = 0.4f), spotColor = tint.copy(alpha = 0.6f))
                .clip(CircleShape).background(tint).clickable(enabled = enabled, onClick = onClick),
            contentAlignment = Alignment.Center,
        ) {
            val icon = when (phase) {
                Phase.Listening -> Icons.Rounded.Stop
                Phase.Speaking -> Icons.Rounded.GraphicEq
                Phase.Error -> Icons.Rounded.Refresh
                else -> Icons.Rounded.Mic
            }
            Icon(icon, null, tint = Color.White, modifier = Modifier.size(38.dp))
        }
    }
}
