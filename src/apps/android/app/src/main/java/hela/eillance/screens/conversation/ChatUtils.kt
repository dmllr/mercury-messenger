package hela.eillance.screens.conversation

import android.util.Patterns
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextDecoration
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

fun buildMessageContent(content: String): AnnotatedString {
  return buildAnnotatedString {
    append(content)
    val matcher = Patterns.WEB_URL.matcher(content)
    while (matcher.find()) {
      val start = matcher.start()
      val end = matcher.end()
      val url = content.substring(start, end)
      val finalUrl = if (url.startsWith("http://") || url.startsWith("https://")) url else "https://$url"
      addLink(
        LinkAnnotation.Url(
          url = finalUrl,
          styles = TextLinkStyles(
            style = SpanStyle(textDecoration = TextDecoration.Underline)
          )
        ),
        start,
        end
      )
    }
  }
}
