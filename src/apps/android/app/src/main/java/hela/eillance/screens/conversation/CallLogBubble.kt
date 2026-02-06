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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.CallMade
import androidx.compose.material.icons.automirrored.filled.CallMissed
import androidx.compose.material.icons.automirrored.filled.CallReceived
import androidx.compose.material.icons.filled.Call
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallStatus
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryEntryKind
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.concurrent.TimeUnit

@Composable
fun CallLogBubble(
  entry: HistoryEntry,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>,
  isUnread: Boolean = false
) {
  val (icon, color, text) = when (entry.callDirection) {
    CallDirection.Outgoing -> when (entry.callStatus) {
      CallStatus.Answered -> Triple(
        Icons.AutoMirrored.Filled.CallMade,
        MaterialTheme.colorScheme.primary,
        if (entry.kind == HistoryEntryKind.VideoCall) stringResource(R.string.call_outgoing_video) else stringResource(
          R.string.call_outgoing_voice
        )
      )

      else -> Triple(
        Icons.AutoMirrored.Filled.CallMade,
        Color.Red,
        if (entry.kind == HistoryEntryKind.VideoCall) stringResource(R.string.call_outgoing_video) else stringResource(
          R.string.call_outgoing_voice
        )
      )
    }

    CallDirection.Incoming -> when (entry.callStatus) {
      CallStatus.Answered -> Triple(
        Icons.AutoMirrored.Filled.CallReceived,
        MaterialTheme.colorScheme.primary,
        if (entry.kind == HistoryEntryKind.VideoCall) stringResource(R.string.call_incoming_video) else stringResource(
          R.string.call_incoming_voice
        )
      )

      CallStatus.Missed -> Triple(
        Icons.AutoMirrored.Filled.CallMissed,
        Color.Red,
        stringResource(R.string.call_missed)
      )

      else -> Triple(
        Icons.AutoMirrored.Filled.CallReceived,
        Color.Red,
        stringResource(R.string.call_rejected)
      )
    }

    null -> Triple(
      Icons.Default.Call,
      MaterialTheme.colorScheme.onSurfaceVariant,
      stringResource(R.string.call_generic)
    )
  }

  val formattedTime = formatTimestamp(entry.timestamp, zoneId, formatters)

  val duration = entry.durationSeconds
  val durationText = if (entry.callStatus == CallStatus.Answered && duration > 0) {
    val minutes = TimeUnit.SECONDS.toMinutes(duration.toLong())
    val seconds =
      duration - TimeUnit.MINUTES.toSeconds(duration.toLong() - TimeUnit.MINUTES.toSeconds(minutes))
    stringResource(id = R.string.call_duration_format, minutes, seconds)
  } else ""

  Row(
    modifier = Modifier
      .fillMaxWidth()
      .padding(vertical = 8.dp),
    horizontalArrangement = Arrangement.Center,
    verticalAlignment = Alignment.CenterVertically
  ) {
    Icon(
      imageVector = icon,
      contentDescription = text,
      tint = color,
      modifier = Modifier.padding(end = 8.dp)
    )
    Text(
      text = "$text - $formattedTime$durationText",
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
