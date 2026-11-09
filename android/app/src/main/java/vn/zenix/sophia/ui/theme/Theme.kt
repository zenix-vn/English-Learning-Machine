package vn.zenix.sophia.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import vn.zenix.sophia.R

/** The palette of the device (firmware main/home/ui_kit.h): a warm cream page, deep ink text, one colour per programme. */
object Palette {
    val Bg = Color(0xFFFFF8EC)
    val Ink = Color(0xFF1F2340)
    val Muted = Color(0xFF4A4F6A)
    val Faint = Color(0xFF8A8FA8)
    val Rim = Color(0xFFE9DCC6)
    val Card = Color(0xFFFFFFFF)
    val Soft = Color(0xFFF3F5F9)

    val Blue = Color(0xFF2563EB); val BlueLight = Color(0xFF60A5FA)
    val Orange = Color(0xFFEA580C); val OrangeLight = Color(0xFFFDA55A)
    val Teal = Color(0xFF0D9488); val TealLight = Color(0xFF5EEAD4)
    val Purple = Color(0xFF7C3AED); val PurpleLight = Color(0xFFB69CFB)
    val Pink = Color(0xFFDB2777); val PinkLight = Color(0xFFF783B9)
    val Green = Color(0xFF16A34A); val GreenLight = Color(0xFF5BE08E)
    val Indigo = Color(0xFF4F46E5); val IndigoLight = Color(0xFF818CF8)
    val Cyan = Color(0xFF0891B2); val CyanLight = Color(0xFF67E8F9)
    val Red = Color(0xFFDC2626)
    val Amber = Color(0xFFD97706)
    val AmberSoft = Color(0xFFFFE3C2)
}

val Baloo = FontFamily(
    Font(R.font.baloo2_semibold, FontWeight.Normal),
    Font(R.font.baloo2_semibold, FontWeight.SemiBold),
    Font(R.font.baloo2_extrabold, FontWeight.Bold),
    Font(R.font.baloo2_extrabold, FontWeight.ExtraBold),
)

private fun style(size: Int, weight: FontWeight, line: Int = size + 8) =
    TextStyle(fontFamily = Baloo, fontWeight = weight, fontSize = size.sp, lineHeight = line.sp)

private val AppTypography = Typography(
    displayLarge = style(56, FontWeight.ExtraBold, 64),
    displayMedium = style(44, FontWeight.ExtraBold, 52),
    headlineLarge = style(32, FontWeight.ExtraBold, 38),
    headlineMedium = style(26, FontWeight.ExtraBold, 32),
    headlineSmall = style(22, FontWeight.ExtraBold, 28),
    titleLarge = style(22, FontWeight.ExtraBold, 28),
    titleMedium = style(18, FontWeight.ExtraBold, 24),
    titleSmall = style(16, FontWeight.ExtraBold, 22),
    bodyLarge = style(18, FontWeight.SemiBold, 26),
    bodyMedium = style(16, FontWeight.SemiBold, 23),
    bodySmall = style(14, FontWeight.SemiBold, 20),
    labelLarge = style(16, FontWeight.ExtraBold, 22),
    labelMedium = style(14, FontWeight.ExtraBold, 18),
    labelSmall = style(12, FontWeight.ExtraBold, 16),
)

private val Colors = lightColorScheme(
    primary = Palette.Purple,
    onPrimary = Color.White,
    secondary = Palette.Blue,
    background = Palette.Bg,
    onBackground = Palette.Ink,
    surface = Palette.Card,
    onSurface = Palette.Ink,
    surfaceVariant = Palette.Soft,
    onSurfaceVariant = Palette.Muted,
    outline = Palette.Rim,
    error = Palette.Red,
)

private val AppShapes = Shapes(
    small = RoundedCornerShape(14.dp),
    medium = RoundedCornerShape(20.dp),
    large = RoundedCornerShape(28.dp),
)

@Composable
fun SophiaTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = Colors, typography = AppTypography, shapes = AppShapes, content = content)
}
