package hela.eillance.theme

import androidx.compose.material3.ListItemColors
import androidx.compose.material3.ListItemDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Dp

/**
 * App-specific ListItem that defaults to a transparent background.
 */
@Composable
fun ListItem(
  headlineContent: @Composable () -> Unit,
  modifier: Modifier = Modifier,
  overlineContent: @Composable (() -> Unit)? = null,
  supportingContent: @Composable (() -> Unit)? = null,
  leadingContent: @Composable (() -> Unit)? = null,
  trailingContent: @Composable (() -> Unit)? = null,
  colors: ListItemColors = ListItemDefaults.colors(
    containerColor = Color.Transparent
  ),
  tonalElevation: Dp = ListItemDefaults.Elevation,
  shadowElevation: Dp = ListItemDefaults.Elevation
) {
  androidx.compose.material3.ListItem(
    headlineContent = headlineContent,
    modifier = modifier,
    overlineContent = overlineContent,
    supportingContent = supportingContent,
    leadingContent = leadingContent,
    trailingContent = trailingContent,
    colors = colors,
    tonalElevation = tonalElevation,
    shadowElevation = shadowElevation
  )
}
