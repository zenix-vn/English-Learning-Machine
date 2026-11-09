package vn.zenix.sophia.ui

import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.runtime.Composable
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import vn.zenix.sophia.data.Focus
import vn.zenix.sophia.ui.screens.DictionaryScreen
import vn.zenix.sophia.ui.screens.HomeScreen
import vn.zenix.sophia.ui.screens.OnboardingScreen
import vn.zenix.sophia.ui.screens.SettingsScreen
import vn.zenix.sophia.ui.screens.StatsScreen
import vn.zenix.sophia.ui.screens.StudyScreen
import vn.zenix.sophia.ui.screens.VoiceScreen

private const val HOME = "home"
private const val ONBOARDING = "onboarding"

private val enter: () -> EnterTransition = { slideInHorizontally(tween(280)) { it / 5 } + fadeIn(tween(280)) }
private val exit: () -> ExitTransition = { fadeOut(tween(180)) + scaleOut(tween(220), targetScale = 0.97f) }
private val popEnter: () -> EnterTransition = { fadeIn(tween(240)) + scaleIn(tween(240), initialScale = 0.97f) }
private val popExit: () -> ExitTransition = { slideOutHorizontally(tween(240)) { it / 5 } + fadeOut(tween(200)) }

@Composable
fun AppNav(startConnected: Boolean) {
    val nav: NavHostController = rememberNavController()

    fun toOnboarding() = nav.navigate(ONBOARDING) { popUpTo(0) { inclusive = true } }
    fun toHome() = nav.navigate(HOME) { popUpTo(0) { inclusive = true } }

    NavHost(
        navController = nav,
        startDestination = if (startConnected) HOME else ONBOARDING,
        enterTransition = { enter() }, exitTransition = { exit() },
        popEnterTransition = { popEnter() }, popExitTransition = { popExit() },
    ) {
        composable(ONBOARDING) {
            OnboardingScreen(viewModel<SessionViewModel>(), onReady = { toHome() })
        }
        composable(HOME) {
            HomeScreen(
                vm = viewModel<HomeViewModel>(),
                onStudy = { nav.navigate("study/$it") },
                onVoice = { nav.navigate("voice/${it.route}") },
                onDictionary = { nav.navigate("dictionary") },
                onStats = { nav.navigate("stats") },
                onSettings = { nav.navigate("settings") },
                onNeedsLogin = { toOnboarding() },
            )
        }
        composable("study/{mode}") { entry ->
            val focus = if (entry.arguments?.getString("mode") == "review") Focus.Review else Focus.New
            StudyScreen(focus, viewModel<StudyViewModel>(), onBack = { nav.popBackStack() })
        }
        composable("voice/{mode}") { entry ->
            val mode = VoiceMode.fromRoute(entry.arguments?.getString("mode"))
            VoiceScreen(mode, viewModel<VoiceViewModel>(), onBack = { nav.popBackStack() })
        }
        composable("dictionary") { DictionaryScreen(viewModel<DictionaryViewModel>(), onBack = { nav.popBackStack() }) }
        composable("stats") { StatsScreen(viewModel<StatsViewModel>(), onBack = { nav.popBackStack() }) }
        composable("settings") {
            SettingsScreen(viewModel<SettingsViewModel>(), onBack = { nav.popBackStack() }, onUnlinked = { toOnboarding() })
        }
    }
}
