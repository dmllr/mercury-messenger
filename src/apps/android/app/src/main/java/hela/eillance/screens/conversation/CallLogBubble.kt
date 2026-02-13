package hela.eillance.screens.conversation

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import hela.eillance.ndkio.HistoryEntry
import java.time.ZoneId
import java.time.format.DateTimeFormatter

@Composable
fun CallLogBubble(
  entry: HistoryEntry,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>,
  isUnread: Boolean = false
) {
  val context = LocalContext.current

  val (icon, isError, _) = CallUtils.getCallIconInfo(entry.callDirection, entry.callStatus)
  val color = if (isError) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary

  val text = CallUtils.formatCallText(
    context,
    entry.kind,
    entry.callDirection,
    entry.callStatus,
    entry.durationSeconds
  )

  val formattedTime = formatTimestamp(entry.timestamp, zoneId, formatters)

  Row(
    modifier = Modifier
      .fillMaxWidth()
      .padding(vertical = 8.dp),
    horizontalArrangement = Arrangement.Center,
    verticalAlignment = Alignment.CenterVertically
  ) {
    Icon(
      imageVector = icon,
      contentDescription = null,
      tint = color,
      modifier = Modifier.padding(end = 8.dp)
    )
    Text(
      text = "$text • $formattedTime",
      style = MaterialTheme.typography.bodySmall,
      color = MaterialTheme.colorScheme.onSurfaceVariant
    )
    AnimatedVisibility(
      visible = isUnread,
      enter = scaleIn() + fadeIn(),
      exit = scaleOut() + fadeOut()
    ) {
      Box(
        modifier = Modifier
          .padding(start = 8.dp)
          .size(4.dp)
          .background(MaterialTheme.colorScheme.primary, CircleShape)
      )
    }
  }
}
