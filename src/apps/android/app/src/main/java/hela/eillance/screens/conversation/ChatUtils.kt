package hela.eillance.screens.conversation

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import hela.eillance.camera.R
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter

@Composable
fun formatTimestamp(
  timestamp: Long,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>
): String {
  val messageTime = Instant.ofEpochMilli(timestamp / 1000).atZone(zoneId)
  val now = ZonedDateTime.now(zoneId)

  val isToday = messageTime.toLocalDate().isEqual(now.toLocalDate())
  val isYesterday = messageTime.toLocalDate().isEqual(now.toLocalDate().minusDays(1))

  return when {
    isToday -> messageTime.format(formatters.first)
    isYesterday -> stringResource(
      R.string.time_yesterday_with_time,
      messageTime.format(formatters.first)
    )

    messageTime.year == now.year -> messageTime.format(formatters.second)
    else -> messageTime.format(formatters.third)
  }
}
