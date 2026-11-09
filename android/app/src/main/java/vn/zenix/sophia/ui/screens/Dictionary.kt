package vn.zenix.sophia.ui.screens

import androidx.compose.foundation.ExperimentalFoundationApi
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import vn.zenix.sophia.ui.DictEntry
import vn.zenix.sophia.ui.DictionaryViewModel
import vn.zenix.sophia.ui.components.CenterMessage
import vn.zenix.sophia.ui.components.TopBar
import vn.zenix.sophia.ui.theme.Palette

@OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)
@Composable
fun DictionaryScreen(vm: DictionaryViewModel, onBack: () -> Unit) {
    val entries by vm.entries.collectAsState()
    val loading by vm.loading.collectAsState()
    var query by remember { mutableStateOf("") }
    var letter by remember { mutableStateOf<Char?>(null) }
    var selected by remember { mutableStateOf<DictEntry?>(null) }
    DisposableEffect(Unit) { onDispose { vm.stopAudio() } }

    val shown = remember(entries, query, letter) {
        val q = query.trim().lowercase()
        entries.filter { e ->
            (q.isEmpty() || e.word.lowercase().contains(q)) && (letter == null || e.word.firstOrNull()?.lowercaseChar() == letter)
        }
    }
    val letters = remember(entries) { entries.mapNotNull { it.word.firstOrNull()?.lowercaseChar() }.filter { it in 'a'..'z' }.distinct().sorted() }

    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Palette.Teal.copy(alpha = 0.16f), Palette.Bg, Palette.Bg), endY = 900f))) {
        Column(Modifier.fillMaxSize()) {
            TopBar("Từ điển", onBack)
            if (!loading && !vm.hasPack) {
                CenterMessage("Chưa có bộ từ", "Quay lại trang chính và bấm \"Tải bộ từ\" để có từ điển offline.")
                return@Column
            }
            OutlinedTextField(
                value = query, onValueChange = { query = it; letter = null }, singleLine = true,
                placeholder = { Text("Tìm từ tiếng Anh…", color = Palette.Faint) },
                leadingIcon = { Icon(Icons.Rounded.Search, null, tint = Palette.Teal) },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp), shape = RoundedCornerShape(20.dp),
                colors = OutlinedTextFieldDefaults.colors(
                    focusedBorderColor = Palette.Teal, unfocusedBorderColor = Color.Transparent,
                    focusedContainerColor = Color.White, unfocusedContainerColor = Color.White,
                ),
            )
            Spacer(Modifier.height(10.dp))
            LazyRow(contentPadding = PaddingValues(horizontal = 18.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(letters) { c ->
                    val on = letter == c
                    Box(
                        Modifier.size(40.dp).clip(CircleShape).background(if (on) Palette.Teal else Color.White)
                            .clickable { letter = if (on) null else c; query = "" },
                        contentAlignment = Alignment.Center,
                    ) { Text(c.uppercase(), color = if (on) Color.White else Palette.Ink, style = MaterialTheme.typography.titleSmall) }
                }
            }
            Spacer(Modifier.height(6.dp))
            Text("${shown.size} từ", style = MaterialTheme.typography.bodySmall, color = Palette.Muted, modifier = Modifier.padding(horizontal = 22.dp, vertical = 4.dp))

            val groups = remember(shown) { shown.groupBy { it.word.first().uppercaseChar() } }
            LazyColumn(
                Modifier.fillMaxSize(), contentPadding = PaddingValues(start = 18.dp, end = 18.dp, bottom = 24.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                groups.forEach { (initial, words) ->
                    stickyHeader(key = "h$initial") {
                        Text(
                            "$initial", style = MaterialTheme.typography.titleMedium, color = Palette.Teal,
                            modifier = Modifier.fillMaxWidth().background(Palette.Bg).padding(vertical = 4.dp, horizontal = 4.dp),
                        )
                    }
                    items(words, key = { it.index }) { e ->
                        val card = remember(e.index) { vm.card(e.index) }
                        Row(
                            Modifier.fillMaxWidth().clip(RoundedCornerShape(18.dp)).background(Color.White)
                                .clickable { selected = e }.padding(horizontal = 16.dp, vertical = 12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Column(Modifier.weight(1f)) {
                                Text(e.word, style = MaterialTheme.typography.titleMedium, color = Palette.Ink)
                                if (card != null) {
                                    Text(card.meaning, style = MaterialTheme.typography.bodySmall, color = Palette.Muted, maxLines = 1)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    selected?.let { e ->
        val card = vm.card(e.index)
        ModalBottomSheet(
            onDismissRequest = { vm.stopAudio(); selected = null },
            sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
            containerColor = Palette.Bg,
        ) {
            if (card != null) {
                Column(Modifier.fillMaxWidth().navigationBarsPadding().padding(horizontal = 24.dp, vertical = 8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(card.word, style = MaterialTheme.typography.displayMedium, color = Palette.Ink)
                            if (card.ipa.isNotEmpty()) Text(card.ipa, fontFamily = FontFamily.Default, fontSize = 20.sp, color = Palette.Muted)
                        }
                        if (vm.hasAudio(e.index)) {
                            Box(
                                Modifier.size(56.dp).clip(CircleShape).background(Palette.Teal).clickable { vm.speak(e.index) },
                                contentAlignment = Alignment.Center,
                            ) { Icon(Icons.AutoMirrored.Rounded.VolumeUp, "Nghe", tint = Color.White, modifier = Modifier.size(28.dp)) }
                        }
                    }
                    Spacer(Modifier.height(14.dp))
                    Text("NGHĨA", style = MaterialTheme.typography.labelMedium, color = Palette.Faint)
                    Text(card.meaning, style = MaterialTheme.typography.headlineSmall, color = Palette.Ink)
                    if (card.exampleEnglish.isNotEmpty()) {
                        Spacer(Modifier.height(16.dp))
                        Text("VÍ DỤ", style = MaterialTheme.typography.labelMedium, color = Palette.Faint)
                        Text(card.exampleEnglish, style = MaterialTheme.typography.titleLarge, color = Palette.Ink)
                        if (card.exampleVietnamese.isNotEmpty()) Text(card.exampleVietnamese, style = MaterialTheme.typography.bodyLarge, color = Palette.Muted)
                    }
                    Spacer(Modifier.height(24.dp))
                }
            }
        }
    }
}
