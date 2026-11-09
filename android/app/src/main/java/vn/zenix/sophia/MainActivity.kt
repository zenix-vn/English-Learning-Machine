package vn.zenix.sophia

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import vn.zenix.sophia.ui.AppNav
import vn.zenix.sophia.ui.theme.Palette
import vn.zenix.sophia.ui.theme.SophiaTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val container = (application as SophiaApp).container
        setContent {
            SophiaTheme {
                Box(Modifier.fillMaxSize().background(Palette.Bg)) {
                    AppNav(startConnected = container.prefs.activated)
                }
            }
        }
    }
}
