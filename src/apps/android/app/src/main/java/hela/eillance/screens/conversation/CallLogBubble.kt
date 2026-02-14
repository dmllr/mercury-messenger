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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import hela.eillance.components.CallEntryDisplay
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
  val formattedTime = formatTimestamp(entry.timestamp, zoneId, formatters)

  Row(
    modifier = Modifier
      .fillMaxWidth()
      .padding(vertical = 8.dp),
    horizontalArrangement = Arrangement.Center,
    verticalAlignment = Alignment.CenterVertically
  ) {
    CallEntryDisplay(
      kind = entry.kind,
      direction = entry.callDirection,
      status = entry.callStatus,
      durationSeconds = entry.durationSeconds,
      textStyle = MaterialTheme.typography.bodySmall,
      // For bubble: Use Primary color for normal calls, Error for missed/rejected
      baseColor = MaterialTheme.colorScheme.primary
    )

    Text(
      text = " ΓÇó $formattedTime",
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
