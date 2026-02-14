package hela.eillance.components

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallStatus
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.screens.conversation.CallUtils

@Composable
fun CallEntryDisplay(
  kind: HistoryEntryKind,
  direction: CallDirection?,
  status: CallStatus?,
  durationSeconds: Int,
  modifier: Modifier = Modifier,
  textStyle: TextStyle = MaterialTheme.typography.bodyMedium,
  baseColor: Color = MaterialTheme.colorScheme.onSurface,
  iconSize: Dp = 16.dp, // Roughly same size as text height for bodyMedium
  maxLines: Int = 1
) {
  val context = LocalContext.current
  val (icon, isError, _) = CallUtils.getCallIconInfo(direction, status)

  // If the call status indicates an error (e.g. Missed/Rejected), prioritize Error color.
  // Otherwise use the provided baseColor (which might be Primary for Bubble, or OnSurfaceVariant for List).
  val finalColor = if (isError) MaterialTheme.colorScheme.error else baseColor

  val text = CallUtils.formatCallText(
    context,
    kind,
    direction,
    status,
    durationSeconds
  )

  Row(
    modifier = modifier,
    verticalAlignment = Alignment.CenterVertically
  ) {
    Icon(
      imageVector = icon,
      contentDescription = null,
      tint = finalColor,
      modifier = Modifier.size(iconSize)
    )
    Spacer(modifier = Modifier.width(4.dp))
    Text(
      text = text,
      style = textStyle,
      color = finalColor,
      maxLines = maxLines,
      overflow = TextOverflow.Ellipsis
    )
  }
}
