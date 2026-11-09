package vn.zenix.sophia.ui.components

import androidx.annotation.DrawableRes
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
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
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import vn.zenix.sophia.R
import vn.zenix.sophia.ui.theme.Palette
import vn.zenix.sophia.voice.Phase
import kotlin.random.Random

enum class SophiaFrame(@DrawableRes val res: Int) {
    Idle(R.drawable.sophia_idle), Blink(R.drawable.sophia_blink), Talk1(R.drawable.sophia_talk1),
    Talk2(R.drawable.sophia_talk2), Listen(R.drawable.sophia_listen), Think(R.drawable.sophia_think),
    Happy(R.drawable.sophia_happy), Wave(R.drawable.sophia_wave),
}

@Composable
fun SophiaImage(frame: SophiaFrame, size: Dp, modifier: Modifier = Modifier) {
    Image(
        painter = painterResource(frame.res), contentDescription = "Sophia",
        modifier = modifier.size(size), contentScale = ContentScale.Fit,
    )
}

/** Sophia as on the device: waves while connecting, tilts her head to listen, looks up to think, moves her mouth to speak. */
@Composable
fun AnimatedSophia(phase: Phase, size: Dp, modifier: Modifier = Modifier, bounce: Boolean = true) {
    var tick by remember { mutableIntStateOf(0) }
    var blink by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { while (true) { delay(150); tick++ } }
    LaunchedEffect(Unit) {
        while (true) {
            delay(Random.nextLong(3000, 5000))
            blink = true
            delay(200)
            blink = false
        }
    }
    val mouth = remember { listOf(SophiaFrame.Talk1, SophiaFrame.Talk2, SophiaFrame.Talk2, SophiaFrame.Talk1, SophiaFrame.Idle, SophiaFrame.Talk2, SophiaFrame.Talk1, SophiaFrame.Talk2) }
    val frame = when (phase) {
        Phase.Speaking -> mouth[tick % mouth.size]
        Phase.Connecting -> SophiaFrame.Wave
        Phase.Thinking -> SophiaFrame.Think
        Phase.Listening -> if (blink) SophiaFrame.Blink else SophiaFrame.Listen
        else -> if (blink) SophiaFrame.Blink else SophiaFrame.Idle
    }
    val float = rememberInfiniteTransition(label = "float")
    val dy by float.animateFloat(
        initialValue = 0f, targetValue = if (bounce) -6f else 0f,
        animationSpec = infiniteRepeatable(tween(1600, easing = FastOutSlowInEasing), RepeatMode.Reverse), label = "dy",
    )
    SophiaImage(frame, size, modifier.graphicsLayer { translationY = dy * density })
}

/** A big colourful tile of the home screen: gradient, icon, name, an optional count. */
@Composable
fun TileCard(
    title: String, icon: ImageVector, color: Color, light: Color,
    modifier: Modifier = Modifier, badge: Int? = null, onClick: () -> Unit,
) {
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    val scale by animateFloatAsState(if (pressed) 0.95f else 1f, tween(120), label = "press")
    val shape = RoundedCornerShape(28.dp)
    Box(
        modifier
            .scale(scale)
            .height(136.dp)
            .shadow(10.dp, shape, ambientColor = color.copy(alpha = 0.35f), spotColor = color.copy(alpha = 0.5f))
            .clip(shape)
            .background(Brush.verticalGradient(listOf(light, color)))
            .clickable(interactionSource = source, indication = null, onClick = onClick),
    ) {
        Box(
            Modifier.padding(16.dp).size(54.dp).clip(CircleShape).background(Color.White.copy(alpha = 0.24f)),
            contentAlignment = Alignment.Center,
        ) { Icon(icon, null, tint = Color.White, modifier = Modifier.size(30.dp)) }
        Text(
            title, color = Color.White, style = MaterialTheme.typography.titleLarge,
            modifier = Modifier.align(Alignment.BottomStart).padding(start = 16.dp, bottom = 14.dp, end = 12.dp),
            maxLines = 1,
        )
        if (badge != null && badge > 0) {
            Box(
                Modifier.align(Alignment.TopEnd).padding(12.dp).clip(RoundedCornerShape(50)).background(Color.White)
                    .padding(horizontal = 10.dp, vertical = 2.dp),
            ) { Text("$badge", color = color, style = MaterialTheme.typography.labelLarge) }
        }
    }
}

/** White rounded surface with a soft shadow, the base of cards and panels. */
@Composable
fun SoftCard(modifier: Modifier = Modifier, color: Color = Palette.Card, content: @Composable () -> Unit) {
    val shape = RoundedCornerShape(26.dp)
    Box(
        modifier
            .shadow(6.dp, shape, ambientColor = Palette.Ink.copy(alpha = 0.08f), spotColor = Palette.Ink.copy(alpha = 0.12f))
            .clip(shape).background(color),
    ) { content() }
}

@Composable
fun BigButton(
    text: String, onClick: () -> Unit, modifier: Modifier = Modifier, color: Color = Palette.Purple,
    icon: ImageVector? = null, enabled: Boolean = true, contentColor: Color = Color.White,
    horizontalPadding: Dp = 20.dp,
) {
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    val scale by animateFloatAsState(if (pressed) 0.96f else 1f, tween(100), label = "press")
    val shape = RoundedCornerShape(22.dp)
    Row(
        modifier
            .scale(scale)
            .alpha(if (enabled) 1f else 0.45f)
            .height(56.dp)
            .shadow(if (enabled) 8.dp else 0.dp, shape, ambientColor = color.copy(alpha = 0.3f), spotColor = color.copy(alpha = 0.45f))
            .clip(shape).background(color)
            .clickable(interactionSource = source, indication = null, enabled = enabled, onClick = onClick)
            .padding(horizontal = horizontalPadding),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center,
    ) {
        if (icon != null) {
            Icon(icon, null, tint = contentColor, modifier = Modifier.size(24.dp))
            Spacer(Modifier.width(if (horizontalPadding < 20.dp) 6.dp else 10.dp))
        }
        Text(text, color = contentColor, style = MaterialTheme.typography.titleMedium, maxLines = 1)
    }
}

@Composable
fun TopBar(title: String, onBack: () -> Unit, modifier: Modifier = Modifier, trailing: @Composable () -> Unit = {}) {
    Row(
        modifier.fillMaxWidth().statusBarsPadding().padding(horizontal = 16.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(44.dp).shadow(4.dp, CircleShape, ambientColor = Palette.Ink.copy(alpha = 0.1f))
                .clip(CircleShape).background(Color.White).clickable(onClick = onBack),
            contentAlignment = Alignment.Center,
        ) { Icon(Icons.AutoMirrored.Rounded.ArrowBack, "Quay lại", tint = Palette.Ink) }
        Spacer(Modifier.width(14.dp))
        Text(title, style = MaterialTheme.typography.headlineMedium, color = Palette.Ink, modifier = Modifier.weight(1f), maxLines = 1)
        trailing()
    }
}

/** A small rounded label: a count, a status. */
@Composable
fun Pill(text: String, color: Color, modifier: Modifier = Modifier, icon: ImageVector? = null, background: Color = color.copy(alpha = 0.14f)) {
    Row(
        modifier.clip(RoundedCornerShape(50)).background(background).padding(horizontal = 12.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icon != null) {
            Icon(icon, null, tint = color, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(5.dp))
        }
        Text(text, color = color, style = MaterialTheme.typography.labelLarge, maxLines = 1)
    }
}

@Composable
fun ProgressBar(fraction: Float, color: Color, modifier: Modifier = Modifier, height: Dp = 12.dp) {
    val animated by animateFloatAsState(fraction.coerceIn(0f, 1f), tween(400), label = "bar")
    Box(modifier.fillMaxWidth().height(height).clip(RoundedCornerShape(50)).background(Palette.Rim)) {
        Box(Modifier.fillMaxWidth(animated).height(height).clip(RoundedCornerShape(50)).background(color))
    }
}

@Composable
fun CenterMessage(title: String, body: String, modifier: Modifier = Modifier, action: (@Composable () -> Unit)? = null) {
    Column(modifier.fillMaxWidth().padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        SophiaImage(SophiaFrame.Think, 120.dp)
        Spacer(Modifier.height(12.dp))
        Text(title, style = MaterialTheme.typography.headlineSmall, color = Palette.Ink, textAlign = TextAlign.Center)
        Spacer(Modifier.height(6.dp))
        Text(body, style = MaterialTheme.typography.bodyMedium, color = Palette.Muted, textAlign = TextAlign.Center)
        if (action != null) {
            Spacer(Modifier.height(20.dp))
            action()
        }
    }
}

/** Clips to a rounded rectangle with the given corner radius in dp. */
fun Modifier.clipToRound(radiusDp: Int): Modifier = clip(RoundedCornerShape(radiusDp.dp))
