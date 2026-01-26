package hela.eillance.theme

import android.app.Activity
import android.content.Context
import android.content.res.Configuration
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat

@Immutable
data class ExtendedColors(
  val success: Color,
  val warning: Color,
  val offline: Color,
)

val LightExtendedColors = ExtendedColors(
  success = Color(0xFF388E3C),
  warning = Color(0xFFFFB74D),
  offline = Color(0xFF9E9E9E),
)

val DarkExtendedColors = ExtendedColors(
  success = Color(0xFF2E7D32),
  warning = Color(0xFFF57F17),
  offline = Color(0xFF616161),
)

val LocalExtendedColors = staticCompositionLocalOf {
  LightExtendedColors
}

private val DarkColorScheme = darkColorScheme(
  primary = Color(0xFFFFFFFF),
  onPrimary = Color(0xFF000000),
  primaryContainer = Color(0xFF000000),
  onPrimaryContainer = Color(0xFFFFFFFF),
  secondary = Color(0xFFB0B0B0),
  onSecondary = Color(0xFF000000),
  secondaryContainer = Color(0xFF424242),
  onSecondaryContainer = Color(0xFFE0E0E0),
  background = Color(0xFF000000),
  surface = Color(0xFF000000),
  onSurface = Color(0xFFE0E0E0),
  surfaceVariant = Color(0xFF1E1E1E),
  onSurfaceVariant = Color(0xFFB0B0B0),
  outline = Color(0xFF555555),
  error = Color(0xFFCF6679)
)

private val LightColorScheme = lightColorScheme(
  primary = Color(0xFF000000),
  onPrimary = Color(0xFFFFFFFF),
  primaryContainer = Color(0xFFFFFFFF),
  onPrimaryContainer = Color(0xFF000000),
  secondary = Color(0xFF424242),
  onSecondary = Color(0xFFFFFFFF),
  secondaryContainer = Color(0xFFF5F5F5),
  onSecondaryContainer = Color(0xFF121212),
  background = Color(0xFFFFFFFF),
  surface = Color(0xFFFFFFFF),
  onSurface = Color(0xFF000000),
  surfaceVariant = Color(0xFFF0F0F0),
  onSurfaceVariant = Color(0xFF424242),
  outline = Color(0xFFBDBDBD),
  error = Color(0xFFB00020)
)

val Typography = Typography(
  bodyLarge = TextStyle(
    fontFamily = FontFamily.Default,
    fontWeight = FontWeight.Normal,
    fontSize = 16.sp,
    lineHeight = 24.sp,
    letterSpacing = 0.5.sp
  ),
)

@Composable
fun EillanceTheme(
  darkTheme: Boolean = isSystemInDarkTheme(),
  content: @Composable () -> Unit
) {
  val colorScheme = when {
    darkTheme -> DarkColorScheme
    else -> LightColorScheme
  }

  val view = LocalView.current
  if (!view.isInEditMode) {
    SideEffect {
      WindowCompat.getInsetsController((view.context as Activity).window, view).apply {
        isAppearanceLightStatusBars = !darkTheme
        isAppearanceLightNavigationBars = !darkTheme
      }
    }
  }

  // Select the correct set of custom colors based on the theme
  val extendedColors = if (darkTheme) DarkExtendedColors else LightExtendedColors

  // Provide the standard MaterialTheme with custom colors
  CompositionLocalProvider(LocalExtendedColors provides extendedColors) {
    MaterialTheme(
      colorScheme = colorScheme,
      typography = Typography,
      content = content
    )
  }
}

@Suppress("unused")
val MaterialTheme.extendedColors: ExtendedColors
  @Composable
  @ReadOnlyComposable
  get() = LocalExtendedColors.current

@Suppress("unused")
val Context.themedColors: ExtendedColors
  get() {
    val nightModeFlags = this.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
    val isDarkMode = nightModeFlags == Configuration.UI_MODE_NIGHT_YES
    return if (isDarkMode) {
      DarkExtendedColors
    } else {
      LightExtendedColors
    }
  }
