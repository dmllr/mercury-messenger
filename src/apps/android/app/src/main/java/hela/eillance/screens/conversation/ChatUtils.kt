package hela.eillance.screens.conversation

import android.util.Patterns
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AttachFile
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
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
      val finalUrl =
        if (url.startsWith("http://") || url.startsWith("https://")) url else "https://$url"
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

@Composable
fun ReplyPreview(
  name: String,
  text: String,
  hasAttachments: Boolean,
  modifier: Modifier = Modifier,
  onClose: (() -> Unit)? = null,
  barColor: Color = MaterialTheme.colorScheme.primary,
  nameColor: Color = MaterialTheme.colorScheme.primary,
  textColor: Color = MaterialTheme.colorScheme.onSurfaceVariant
) {
  Row(
    modifier = modifier,
    verticalAlignment = Alignment.CenterVertically
  ) {
    Column(
      modifier = Modifier
        .weight(1f, fill = false)
        .drawBehind {
          drawLine(
            color = barColor,
            start = androidx.compose.ui.geometry.Offset(2.dp.toPx(), 0f),
            end = androidx.compose.ui.geometry.Offset(2.dp.toPx(), size.height),
            strokeWidth = 4.dp.toPx()
          )
        }
        .padding(start = 12.dp, end = 8.dp, top = 6.dp, bottom = 6.dp),
      verticalArrangement = Arrangement.Center
    ) {
      Text(
        text = name,
        style = MaterialTheme.typography.labelMedium,
        color = nameColor,
        fontWeight = FontWeight.Bold,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis
      )
      if (text.isNotEmpty() || hasAttachments) {
        Spacer(modifier = Modifier.height(2.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
          if (hasAttachments) {
            Icon(
              Icons.Default.AttachFile,
              contentDescription = null,
              modifier = Modifier.size(14.dp),
              tint = textColor
            )
            if (text.isNotEmpty()) {
              Spacer(modifier = Modifier.width(4.dp))
            }
          }
          if (text.isNotEmpty()) {
            Text(
              text = text,
              style = MaterialTheme.typography.bodySmall,
              color = textColor,
              maxLines = 1,
              overflow = TextOverflow.Ellipsis
            )
          }
        }
      }
    }
    if (onClose != null) {
      IconButton(
        onClick = onClose,
        modifier = Modifier.align(Alignment.CenterVertically)
      ) {
        Icon(
          Icons.Default.Close,
          contentDescription = null,
          modifier = Modifier.size(20.dp),
          tint = textColor
        )
      }
    }
  }
}
