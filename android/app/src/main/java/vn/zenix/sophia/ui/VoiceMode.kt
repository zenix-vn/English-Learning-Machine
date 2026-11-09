package vn.zenix.sophia.ui

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Forum
import androidx.compose.material.icons.rounded.Mic
import androidx.compose.material.icons.rounded.School
import androidx.compose.material.icons.rounded.Translate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import vn.zenix.sophia.ui.theme.Palette

/**
 * The four programmes that talk to the AI. `openText` is the line the app sends first; the server reads it to choose the
 * mode (backend/app/session.py: LEARN_ON, CHAT_ON, INTERPRETER_ON, PRONUNCIATION_ON), exactly like the device's tiles.
 */
enum class VoiceMode(
    val route: String,
    val title: String,
    val tagline: String,
    val openText: String,
    val color: Color,
    val light: Color,
    val icon: ImageVector,
    val tip: String,
) {
    Learn(
        "learn", "Học Online", "Bài học cùng Sophia", "Chế độ học online.",
        Palette.Purple, Palette.PurpleLight, Icons.Rounded.School,
        "Sophia dạy từng bài theo chương trình. Chỉ nói chuyện về học tiếng Anh.",
    ),
    Pronounce(
        "pronounce", "Phát âm", "Luyện nói chuẩn", "Mình muốn luyện phát âm.",
        Palette.Pink, Palette.PinkLight, Icons.Rounded.Mic,
        "Sophia đọc từ, bạn nhắc lại, rồi nghe nhận xét.",
    ),
    Translate(
        "translate", "Phiên dịch", "Việt ⇄ Anh ngay lập tức", "Chế độ phiên dịch.",
        Palette.Indigo, Palette.IndigoLight, Icons.Rounded.Translate,
        "Nói tiếng Việt, Sophia dịch sang tiếng Anh. Nói tiếng Anh, Sophia dịch sang tiếng Việt.",
    ),
    Chat(
        "chat", "Trò chuyện", "Nói chuyện tự do", "Chế độ trò chuyện.",
        Palette.Cyan, Palette.CyanLight, Icons.Rounded.Forum,
        "Nói chuyện với Sophia về bất cứ điều gì bạn thích.",
    );

    companion object {
        fun fromRoute(route: String?) = entries.firstOrNull { it.route == route } ?: Learn
    }
}
