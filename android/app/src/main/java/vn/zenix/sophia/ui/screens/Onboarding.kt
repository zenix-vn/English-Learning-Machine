package vn.zenix.sophia.ui.screens

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import vn.zenix.sophia.ui.GateState
import vn.zenix.sophia.ui.SessionViewModel
import vn.zenix.sophia.ui.components.AnimatedSophia
import vn.zenix.sophia.ui.components.BigButton
import vn.zenix.sophia.ui.components.SoftCard
import vn.zenix.sophia.ui.components.clipToRound
import vn.zenix.sophia.ui.theme.Palette
import vn.zenix.sophia.voice.Phase

/** First screen: connect the phone to the server, then wait for the admin to enter the activation code. */
@Composable
fun OnboardingScreen(vm: SessionViewModel, onReady: () -> Unit) {
    val state by vm.state.collectAsState()
    LaunchedEffect(state) { if (state is GateState.Ready) onReady() }

    Box(
        Modifier.fillMaxSize()
            .background(Brush.verticalGradient(listOf(Palette.PurpleLight.copy(alpha = 0.28f), Palette.Bg, Palette.Bg))),
    ) {
        Column(
            Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding()
                .verticalScroll(rememberScrollState()).padding(horizontal = 24.dp, vertical = 16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            val phase = when (state) {
                is GateState.Connecting -> Phase.Thinking
                is GateState.Pending -> Phase.Listening
                else -> Phase.Connecting
            }
            AnimatedSophia(phase, 190.dp)
            Text("Chào bạn! Mình là Sophia", style = MaterialTheme.typography.headlineLarge, color = Palette.Ink, textAlign = TextAlign.Center)
            Spacer(Modifier.height(4.dp))
            Text(
                "Cô giáo tiếng Anh luôn ở trong túi của bạn", style = MaterialTheme.typography.bodyLarge,
                color = Palette.Muted, textAlign = TextAlign.Center,
            )
            Spacer(Modifier.height(24.dp))
            AnimatedContent(state, transitionSpec = { fadeIn() togetherWith fadeOut() }, label = "gate") { s ->
                when (s) {
                    is GateState.Pending -> ActivationCard(s.code, vm.serverUrl, onCancel = vm::back)
                    else -> ConnectCard(vm, s)
                }
            }
        }
    }
}

@Composable
private fun ConnectCard(vm: SessionViewModel, state: GateState) {
    var url by remember { mutableStateOf(vm.serverUrl) }
    val busy = state is GateState.Connecting
    SoftCard(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(20.dp)) {
            Text("Máy chủ", style = MaterialTheme.typography.titleMedium, color = Palette.Ink)
            Spacer(Modifier.height(4.dp))
            Text("Địa chỉ máy chủ Sophia của bạn", style = MaterialTheme.typography.bodySmall, color = Palette.Muted)
            Spacer(Modifier.height(10.dp))
            OutlinedTextField(
                value = url, onValueChange = { url = it }, singleLine = true, enabled = !busy,
                modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(16.dp),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                colors = OutlinedTextFieldDefaults.colors(
                    focusedBorderColor = Palette.Purple, unfocusedBorderColor = Palette.Rim,
                    focusedContainerColor = Palette.Soft, unfocusedContainerColor = Palette.Soft,
                ),
            )
            if (state is GateState.Failed) {
                Spacer(Modifier.height(10.dp))
                Text(state.message, color = Palette.Red, style = MaterialTheme.typography.bodyMedium)
            }
            Spacer(Modifier.height(16.dp))
            if (busy) {
                Box(Modifier.fillMaxWidth().height(56.dp), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(color = Palette.Purple, modifier = Modifier.size(32.dp))
                }
            } else {
                BigButton("Kết nối", onClick = { vm.connect(url) }, modifier = Modifier.fillMaxWidth(), icon = Icons.Rounded.Link)
            }
        }
    }
}

@Composable
private fun ActivationCard(code: String, server: String, onCancel: () -> Unit) {
    SoftCard(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(20.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text("Kích hoạt Sophia", style = MaterialTheme.typography.headlineSmall, color = Palette.Ink)
            Spacer(Modifier.height(4.dp))
            Text("Nhập mã này vào trang quản trị", style = MaterialTheme.typography.bodyMedium, color = Palette.Muted)
            Spacer(Modifier.height(16.dp))
            // the boxes share the width, so a code of any length fits on any phone
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                code.forEach { ch ->
                    Box(
                        Modifier.weight(1f).height(68.dp).clipToRound(14)
                            .background(Brush.verticalGradient(listOf(Color(0xFFA78BFA), Color(0xFF6D28D9)))),
                        contentAlignment = Alignment.Center,
                    ) { Text("$ch", color = Color.White, style = MaterialTheme.typography.displayMedium) }
                }
            }
            Spacer(Modifier.height(18.dp))
            val admin = server.removePrefix("https://").removePrefix("http://").trimEnd('/') + "/admin"
            Steps(admin)
            Spacer(Modifier.height(14.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(color = Palette.Purple, strokeWidth = 3.dp, modifier = Modifier.size(20.dp))
                Spacer(Modifier.size(10.dp))
                Text("Đang chờ kích hoạt…", style = MaterialTheme.typography.bodyMedium, color = Palette.Muted)
            }
            TextButton(onClick = onCancel) { Text("Đổi máy chủ", color = Palette.Purple, style = MaterialTheme.typography.labelLarge) }
        }
    }
}

@Composable
private fun Steps(admin: String) {
    val steps = listOf("Mở trang $admin", "Vào mục Thiết bị", "Nhập mã, bấm Kích hoạt")
    Column(Modifier.fillMaxWidth().clipToRound(18).background(Palette.Soft).padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        steps.forEachIndexed { i, s ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(26.dp).clipToRound(13).background(listOf(Palette.Blue, Palette.Teal, Palette.Purple)[i]), contentAlignment = Alignment.Center) {
                    Text("${i + 1}", color = Color.White, style = MaterialTheme.typography.labelMedium)
                }
                Spacer(Modifier.size(10.dp))
                Text(s, style = MaterialTheme.typography.bodyMedium, color = Palette.Ink)
            }
        }
    }
}
