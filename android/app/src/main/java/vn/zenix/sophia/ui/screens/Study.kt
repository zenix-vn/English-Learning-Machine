package vn.zenix.sophia.ui.screens

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Star
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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import vn.zenix.sophia.data.Card
import vn.zenix.sophia.data.Focus
import vn.zenix.sophia.data.PickKind
import vn.zenix.sophia.data.Quality
import vn.zenix.sophia.ui.StudyUi
import vn.zenix.sophia.ui.StudyViewModel
import vn.zenix.sophia.ui.components.BigButton
import vn.zenix.sophia.ui.components.CenterMessage
import vn.zenix.sophia.ui.components.Pill
import vn.zenix.sophia.ui.components.SoftCard
import vn.zenix.sophia.ui.components.SophiaFrame
import vn.zenix.sophia.ui.components.SophiaImage
import vn.zenix.sophia.ui.components.TopBar
import vn.zenix.sophia.ui.theme.Palette

/** "Học từ mới" and "Ôn tập": one card at a time, tap to see the meaning, then Forgot / Remembered / Easy. */
@Composable
fun StudyScreen(focus: Focus, vm: StudyViewModel, onBack: () -> Unit) {
    val ui by vm.ui.collectAsState()
    LaunchedEffect(focus) { vm.start(focus) }
    val title = if (focus == Focus.New) "Học từ mới" else "Ôn tập"
    val accent = if (focus == Focus.New) Palette.Blue else Palette.Orange

    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(accent.copy(alpha = 0.16f), Palette.Bg, Palette.Bg), endY = 900f))) {
        Column(Modifier.fillMaxSize()) {
            TopBar(title, onBack) { CounterPills(ui, focus) }
            when {
                ui.loading -> Unit
                ui.noPack -> CenterMessage(
                    "Chưa có bộ từ", "Quay lại trang chính và bấm \"Tải bộ từ\" để học nhé.",
                    action = { BigButton("Về trang chính", onBack, color = accent) },
                )
                ui.finished -> Finished(focus, ui, accent, onMore = vm::more, onBack = onBack)
                else -> Cards(ui, accent, vm::reveal, vm::answer, vm::speak)
            }
        }
    }
}

@Composable
private fun CounterPills(ui: StudyUi, focus: Focus) {
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        if (focus == Focus.New) {
            Pill("Mới ${ui.stats.newLeft}", Palette.Blue)
        } else {
            if (ui.stats.learning > 0) Pill("Học lại ${ui.stats.learning}", Palette.Orange)
            Pill("Ôn ${ui.stats.due}", Palette.Purple)
        }
    }
}

@Composable
fun Cards(ui: StudyUi, accent: Color, onReveal: () -> Unit, onAnswer: (Int) -> Unit, onSpeak: () -> Unit) {
    val card = ui.card ?: return
    Column(Modifier.fillMaxSize().navigationBarsPadding().padding(horizontal = 18.dp)) {
        Spacer(Modifier.height(6.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            val (tag, color) = when (ui.kind) {
                PickKind.New -> "MỚI" to Palette.Blue
                PickKind.Learning -> "HỌC LẠI" to Palette.Amber
                PickKind.Due -> "ÔN TẬP" to Palette.Purple
                else -> "ÔN THÊM" to Palette.Faint
            }
            Pill(tag, color)
            Text("Đã trả lời ${ui.answered}", style = MaterialTheme.typography.bodySmall, color = Palette.Muted)
        }
        Spacer(Modifier.height(12.dp))

        Box(Modifier.weight(1f).fillMaxWidth()) {
            AnimatedContent(
                targetState = ui.index,
                transitionSpec = {
                    (slideInVertically(tween(260)) { it / 6 } + fadeIn(tween(260))) togetherWith
                        (slideOutVertically(tween(200)) { -it / 8 } + fadeOut(tween(160)))
                },
                label = "card",
            ) { _ ->
                SoftCard(Modifier.fillMaxSize().clickable(enabled = !ui.revealed) { onReveal() }) {
                    AnimatedContent(
                        targetState = ui.revealed,
                        transitionSpec = { (fadeIn(tween(220)) + scaleIn(initialScale = 0.96f)) togetherWith (fadeOut(tween(120)) + scaleOut(targetScale = 0.98f)) },
                        label = "side",
                    ) { revealed ->
                        if (revealed) Back(card, ui.hasAudio, onSpeak) else Front(card, ui.hasAudio, accent, onSpeak)
                    }
                }
            }
            // "Nhớ · gặp lại sau 3 ngày"
            ToastPill(ui.toast, ui.toastGood, Modifier.align(Alignment.BottomCenter).padding(bottom = 14.dp))
        }

        Spacer(Modifier.height(14.dp))
        Box(Modifier.height(64.dp)) {
            if (!ui.revealed) {
                BigButton("Xem nghĩa", onReveal, Modifier.fillMaxWidth(), accent)
            } else {
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    BigButton("Quên", { onAnswer(Quality.FORGOT) }, Modifier.weight(1f), Palette.Red, Icons.Rounded.Close, horizontalPadding = 8.dp)
                    BigButton("Nhớ", { onAnswer(Quality.GOOD) }, Modifier.weight(1f), Palette.Green, Icons.Rounded.Check, horizontalPadding = 8.dp)
                    BigButton("Dễ", { onAnswer(Quality.EASY) }, Modifier.weight(1f), Palette.Amber, Icons.Rounded.Star, horizontalPadding = 8.dp)
                }
            }
        }
        Spacer(Modifier.height(14.dp))
    }
}

@Composable
private fun SpeakButton(visible: Boolean, accent: Color, onClick: () -> Unit) {
    if (!visible) return
    Box(
        Modifier.size(60.dp).shadow(8.dp, CircleShape, ambientColor = accent.copy(alpha = 0.3f), spotColor = accent.copy(alpha = 0.5f))
            .clip(CircleShape).background(accent).clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Icon(Icons.AutoMirrored.Rounded.VolumeUp, "Nghe", tint = Color.White, modifier = Modifier.size(30.dp)) }
}

@Composable
private fun Front(card: Card, hasAudio: Boolean, accent: Color, onSpeak: () -> Unit) {
    Column(Modifier.fillMaxSize().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        Text(
            card.word, style = MaterialTheme.typography.displayLarge.copy(fontSize = wordSize(card.word)),
            color = Palette.Ink, textAlign = TextAlign.Center,
        )
        if (card.ipa.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            // the Baloo subset has no phonetic letters: the system font draws them
            Text(card.ipa, fontFamily = FontFamily.Default, fontSize = 24.sp, color = Palette.Muted, textAlign = TextAlign.Center)
        }
        Spacer(Modifier.height(28.dp))
        SpeakButton(hasAudio, accent, onSpeak)
        Spacer(Modifier.height(28.dp))
        Text("Chạm để xem nghĩa", style = MaterialTheme.typography.bodyMedium, color = Palette.Faint)
    }
}

@Composable
private fun Back(card: Card, hasAudio: Boolean, onSpeak: () -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(card.word, style = MaterialTheme.typography.displayMedium, color = Palette.Ink)
                if (card.ipa.isNotEmpty()) Text(card.ipa, fontFamily = FontFamily.Default, fontSize = 20.sp, color = Palette.Muted)
            }
            SpeakButton(hasAudio, Palette.Blue, onSpeak)
        }
        Spacer(Modifier.height(14.dp))
        Box(Modifier.fillMaxWidth().height(3.dp).clip(RoundedCornerShape(2.dp)).background(Palette.Rim))
        Spacer(Modifier.height(14.dp))
        Text("NGHĨA", style = MaterialTheme.typography.labelMedium, color = Palette.Faint)
        Text(shortMeaning(card.meaning), style = MaterialTheme.typography.headlineMedium, color = Palette.Ink)
        if (card.exampleEnglish.isNotEmpty()) {
            Spacer(Modifier.height(18.dp))
            Text("VÍ DỤ", style = MaterialTheme.typography.labelMedium, color = Palette.Faint)
            Text(card.exampleEnglish, style = MaterialTheme.typography.titleLarge, color = Palette.Ink)
            if (card.exampleVietnamese.isNotEmpty()) {
                Text(card.exampleVietnamese, style = MaterialTheme.typography.bodyLarge, color = Palette.Muted)
            }
        }
    }
}

@Composable
private fun Finished(focus: Focus, ui: StudyUi, accent: Color, onMore: () -> Unit, onBack: () -> Unit) {
    Column(Modifier.fillMaxSize().navigationBarsPadding().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        SophiaImage(SophiaFrame.Happy, 190.dp)
        Spacer(Modifier.height(8.dp))
        Text(
            if (focus == Focus.New) "Xong từ mới hôm nay!" else if (ui.answered > 0) "Ôn xong rồi!" else "Chưa có từ cần ôn",
            style = MaterialTheme.typography.headlineMedium, color = Palette.Ink, textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(6.dp))
        Text(
            if (ui.answered > 0) "Hôm nay bạn đã trả lời ${ui.answered} thẻ. Giỏi quá!" else "Hôm nay không còn thẻ nào đến hạn.",
            style = MaterialTheme.typography.bodyLarge, color = Palette.Muted, textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(28.dp))
        if (ui.stats.fresh > 0 || ui.stats.due > 0 || focus == Focus.Review) {
            BigButton(if (focus == Focus.New) "Học thêm" else "Ôn thêm", onMore, Modifier.fillMaxWidth(), accent)
            Spacer(Modifier.height(12.dp))
        }
        BigButton("Về trang chính", onBack, Modifier.fillMaxWidth(), Palette.Ink)
    }
}

private fun wordSize(word: String) = when {
    word.length <= 8 -> 56.sp
    word.length <= 12 -> 44.sp
    word.length <= 18 -> 34.sp
    else -> 26.sp
}

/** The first meanings only: a card is read at a glance (same idea as the device's ShortMeaning). */
private fun shortMeaning(meaning: String, max: Int = 160): String {
    val s = meaning.trim()
    if (s.length <= max) return s
    val head = s.take(max)
    val cut = maxOf(head.lastIndexOf(';'), head.lastIndexOf(','))
    return if (cut >= max / 2) head.substring(0, cut).trim() else head.substringBeforeLast(' ').trim() + "…"
}

/** Kept out of the Column above: inside it AnimatedVisibility would resolve to the ColumnScope overload. */
@Composable
private fun ToastPill(text: String, good: Boolean, modifier: Modifier) {
    Box(modifier) {
        AnimatedVisibility(text.isNotEmpty(), enter = fadeIn() + slideInVertically { -it }, exit = fadeOut()) {
            Text(
                text, color = Color.White, style = MaterialTheme.typography.titleSmall,
                modifier = Modifier.clip(RoundedCornerShape(50)).background(if (good) Palette.Green else Palette.Red)
                    .padding(horizontal = 18.dp, vertical = 8.dp),
            )
        }
    }
}
