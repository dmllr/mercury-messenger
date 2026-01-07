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

// Strict Dark Palette (High Contrast / Monochrome)
private val StrictDarkPrimary = Color(0xFFFFFFFF)
private val StrictDarkOnPrimary = Color(0xFF000000)
private val StrictDarkPrimaryContainer = Color(0xFF000000)
private val StrictDarkOnPrimaryContainer = Color(0xFFFFFFFF)
private val StrictDarkSecondary = Color(0xFFB0B0B0)
private val StrictDarkOnSecondary = Color(0xFF000000)
private val StrictDarkSecondaryContainer = Color(0xFF424242)
private val StrictDarkOnSecondaryContainer = Color(0xFFE0E0E0)
private val StrictDarkBackground = Color(0xFF000000) // Pitch black
private val StrictDarkSurface = Color(0xFF121212)
private val StrictDarkSurfaceVariant = Color(0xFF1E1E1E)
private val StrictDarkOnSurface = Color(0xFFE0E0E0)
private val StrictDarkOnSurfaceVariant = Color(0xFFB0B0B0)
private val StrictDarkOutline = Color(0xFF555555)
private val StrictDarkError = Color(0xFFCF6679)

// Strict Light Palette (Clean / Sharp)
private val StrictLightPrimary = Color(0xFF000000)
private val StrictLightOnPrimary = Color(0xFFFFFFFF)
private val StrictLightPrimaryContainer = Color(0xFFFFFFFF)
private val StrictLightOnPrimaryContainer = Color(0xFF000000)
private val StrictLightSecondary = Color(0xFF424242)
private val StrictLightOnSecondary = Color(0xFFFFFFFF)
private val StrictLightSecondaryContainer = Color(0xFFF5F5F5)
private val StrictLightOnSecondaryContainer = Color(0xFF121212)
private val StrictLightBackground = Color(0xFFFFFFFF)
private val StrictLightSurface = Color(0xFFFFFFFF)
private val StrictLightSurfaceVariant = Color(0xFFF0F0F0)
private val StrictLightOnSurface = Color(0xFF000000)
private val StrictLightOnSurfaceVariant = Color(0xFF424242)
private val StrictLightOutline = Color(0xFFBDBDBD)
private val StrictLightError = Color(0xFFB00020)

val LightExtendedColors = ExtendedColors(
  success = Color(0xFF2E7D32),
  warning = Color(0xFFF57F17),
  offline = Color(0xFF9E9E9E),
)

val DarkExtendedColors = ExtendedColors(
  success = Color(0xFF388E3C), // Darker green to support white icons/text
  warning = Color(0xFFFFB74D),
  offline = Color(0xFF616161),
)

val LocalExtendedColors = staticCompositionLocalOf {
  LightExtendedColors
}

private val DarkColorScheme = darkColorScheme(
  primary = StrictDarkPrimary,
  onPrimary = StrictDarkOnPrimary,
  primaryContainer = StrictDarkPrimaryContainer,
  onPrimaryContainer = StrictDarkOnPrimaryContainer,
  secondary = StrictDarkSecondary,
  onSecondary = StrictDarkOnSecondary,
  secondaryContainer = StrictDarkSecondaryContainer,
  onSecondaryContainer = StrictDarkOnSecondaryContainer,
  background = StrictDarkBackground,
  surface = StrictDarkSurface,
  onSurface = StrictDarkOnSurface,
  surfaceVariant = StrictDarkSurfaceVariant,
  onSurfaceVariant = StrictDarkOnSurfaceVariant,
  outline = StrictDarkOutline,
  error = StrictDarkError
)

private val LightColorScheme = lightColorScheme(
  primary = StrictLightPrimary,
  onPrimary = StrictLightOnPrimary,
  primaryContainer = StrictLightPrimaryContainer,
  onPrimaryContainer = StrictLightOnPrimaryContainer,
  secondary = StrictLightSecondary,
  onSecondary = StrictLightOnSecondary,
  secondaryContainer = StrictLightSecondaryContainer,
  onSecondaryContainer = StrictLightOnSecondaryContainer,
  background = StrictLightBackground,
  surface = StrictLightSurface,
  onSurface = StrictLightOnSurface,
  surfaceVariant = StrictLightSurfaceVariant,
  onSurfaceVariant = StrictLightOnSurfaceVariant,
  outline = StrictLightOutline,
  error = StrictLightError
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
