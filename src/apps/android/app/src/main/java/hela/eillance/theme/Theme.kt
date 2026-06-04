package hela.eillance.theme

import android.app.Activity
import android.content.Context
import android.content.res.Configuration
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.LocalRippleConfiguration
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RippleConfiguration
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
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat

@Immutable
data class ExtendedColors(
  val brand: Color,
  val success: Color,
  val warning: Color,
  val offline: Color,
  val sentBubbleBackground: Color,
  val sentBubbleContent: Color,
  val receivedBubbleBackground: Color,
  val receivedBubbleContent: Color,
)

val BrandOrange = Color(0xFFFF6A00)
val BrandOrangeSoft = Color(0xFFFF8A3D)
val BrandOrangeDeep = Color(0xFFCC4E00)

val LightExtendedColors = ExtendedColors(
  brand = BrandOrange,

  success = Color(0xFF2E7D32),
  warning = Color(0xFFED6C02),
  offline = Color(0xFF9E9E9E),

  sentBubbleBackground = Color(0xFFFFFFFF),
  sentBubbleContent = Color(0xFF000000),

  receivedBubbleBackground = Color(0xFFDADADA),
  receivedBubbleContent = Color(0xFF1C1C1C),
)

val DarkExtendedColors = ExtendedColors(
  brand = BrandOrangeDeep,

  success = Color(0xFF66BB6A),
  warning = Color(0xFFFFA726),
  offline = Color(0xFF8A8A8A),

  sentBubbleBackground = Color(0x338C3600),
  sentBubbleContent = Color(0xFFFFE9DD),

  receivedBubbleBackground = Color(0xFF262626),
  receivedBubbleContent = Color(0xFFEAEAEA),
)

val LocalExtendedColors = staticCompositionLocalOf {
  LightExtendedColors
}

private val DarkColorScheme = darkColorScheme(
  primary = BrandOrangeSoft,
  onPrimary = Color(0xFF2A0E00),

  primaryContainer = Color(0xFF2B1408),
  onPrimaryContainer = Color(0xFFFFC7A6),

  secondary = Color(0xFFB0BEC5),
  onSecondary = Color(0xFF111417),

  background = Color(0xFF121212),
  surface = Color(0xFF181818),
  surfaceVariant = Color(0xFF232323),

  onSurface = Color(0xFFE6E6E6),
  onSurfaceVariant = Color(0xFFB0B0B0),

  outline = Color(0xFF3A3A3A),

  error = Color(0xFFFF6B6B),
  onError = Color.Black
)

private val LightColorScheme = lightColorScheme(
  primary = BrandOrangeDeep,
  onPrimary = Color.White,

  primaryContainer = Color(0xFFFFE2D3),
  onPrimaryContainer = Color(0xFF3A1400),

  secondary = Color(0xFF546E7A),

  background = Color(0xFFF7F7F8),
  surface = Color(0xFFFFFFFF),
  surfaceVariant = Color(0xFFEDEDED),

  onSurface = Color(0xFF1A1A1A),
  onSurfaceVariant = Color(0xFF5F5F5F),

  outline = Color(0xFFD0D0D0),

  error = Color(0xFFD32F2F),
  onError = Color.White
)

val Typography = Typography(
  headlineSmall = TextStyle(
    fontWeight = FontWeight.SemiBold,
    fontSize = 20.sp,
    letterSpacing = (-0.2).sp
  ),

  titleMedium = TextStyle(
    fontWeight = FontWeight.Medium,
    fontSize = 16.sp,
    letterSpacing = 0.sp
  ),

  bodyLarge = TextStyle(
    fontSize = 15.sp,
    lineHeight = 22.sp,
    letterSpacing = 0.2.sp
  ),

  bodyMedium = TextStyle(
    fontSize = 14.sp,
    lineHeight = 20.sp,
    letterSpacing = 0.15.sp
  ),

  labelLarge = TextStyle(
    fontWeight = FontWeight.SemiBold,
    fontSize = 13.sp,
    letterSpacing = 0.4.sp
  )
)


@Composable
private fun eillanceRipple(darkTheme: Boolean): RippleConfiguration {
  return RippleConfiguration(
    color = if (darkTheme) {
      Color(0x26FF6A00)   // softer glow for dark UI (≈15%)
    } else {
      Color(0x1AFF6A00)   // very subtle in light mode (≈10%)
    }
  )
}

@Composable
fun EillanceTheme(
  darkTheme: Boolean = isSystemInDarkTheme(),
  content: @Composable () -> Unit
) {
  val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme
  val extendedColors = if (darkTheme) DarkExtendedColors else LightExtendedColors
  val rippleConfig = eillanceRipple(darkTheme)

  val view = LocalView.current
  if (!view.isInEditMode) {
    SideEffect {
      val window = (view.context as Activity).window
      window.statusBarColor = colorScheme.surface.toArgb()
      WindowCompat.getInsetsController(window, view).apply {
        isAppearanceLightStatusBars = !darkTheme
        isAppearanceLightNavigationBars = !darkTheme
      }
    }
  }

  CompositionLocalProvider(
    LocalExtendedColors provides extendedColors,
    LocalRippleConfiguration provides rippleConfig
  ) {
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
