package hela.eillance.screens.conversation

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.CallMade
import androidx.compose.material.icons.automirrored.filled.CallMissed
import androidx.compose.material.icons.automirrored.filled.CallReceived
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.Done
import androidx.compose.material.icons.filled.DoneAll
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.components.MediaBubbleContent
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallStatus
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.MessageStatus
import hela.eillance.theme.extendedColors
import hela.eillance.ui.LocalizationMapping
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.util.concurrent.TimeUnit

private val jsonParser = Json { ignoreUnknownKeys = true }

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun MessageBubble(
  entry: HistoryEntry,
  onShowMore: (Long) -> Unit,
  transferProgress: Map<String, Float>,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>,
  onMediaClick: (List<Attachment>, Int) -> Unit,
  isGroupHead: Boolean = true,
  isGroupTail: Boolean = true,
  onCancelTransfer: (String) -> Unit,
  onResumeTransfer: (String) -> Unit,
  onDiscardTransfer: (String) -> Unit,
  isSelected: Boolean = false,
  isUnread: Boolean = false,
  selectionMode: Boolean = false,
  onToggleSelection: () -> Unit = {}
) {
  val isFromMe = entry.direction == MessageDirection.Sent
  val alignment = if (isFromMe) Alignment.CenterEnd else Alignment.CenterStart
  val colors = if (isFromMe) {
    MaterialTheme.colorScheme.primaryContainer to MaterialTheme.colorScheme.onPrimaryContainer
  } else {
    MaterialTheme.colorScheme.secondaryContainer to MaterialTheme.colorScheme.onSecondaryContainer
  }

  val topStartRadius = if (!isFromMe && !isGroupHead) 4.dp else 16.dp
  val bottomStartRadius = if (!isFromMe && !isGroupTail) 4.dp else 16.dp
  val topEndRadius = if (isFromMe && !isGroupHead) 4.dp else 16.dp
  val bottomEndRadius = if (isFromMe && !isGroupTail) 4.dp else 16.dp

  val bubbleShape = RoundedCornerShape(
    topStart = topStartRadius,
    topEnd = topEndRadius,
    bottomStart = bottomStartRadius,
    bottomEnd = bottomEndRadius
  )

  val topPadding = if (isGroupHead) 8.dp else 0.dp

  Box(
    modifier = Modifier
      .fillMaxWidth()
      .combinedClickable(
        onClick = {
          if (selectionMode) onToggleSelection()
        },
        onLongClick = {
          onToggleSelection()
        }
      )
      .background(if (isSelected) MaterialTheme.colorScheme.primary.copy(alpha = 0.12f) else Color.Transparent)
      .padding(
        start = if (isFromMe) 48.dp else 8.dp,
        end = if (isFromMe) 8.dp else 48.dp,
        top = topPadding
      ),
    contentAlignment = alignment
  ) {
    val bubbleContent = @Composable {
      Column(
        modifier = Modifier
          .clip(bubbleShape)
          .background(colors.first)
          .padding(horizontal = 4.dp, vertical = 4.dp),
        horizontalAlignment = if (isFromMe) Alignment.End else Alignment.Start
      ) {
        // Render Attachments
        if (entry.attachments.isNotEmpty()) {
          MediaBubbleContent(
            attachments = entry.attachments,
            transferProgress = transferProgress,
            isFromMe = isFromMe,
            selectionMode = selectionMode,
            onMediaClick = { clickedAttachment ->
              val index = entry.attachments.indexOf(clickedAttachment)
              onMediaClick(entry.attachments, if (index != -1) index else 0)
            },
            onCancel = onCancelTransfer,
            onResume = onResumeTransfer,
            onDiscard = onDiscardTransfer
          )

          if (!entry.content.isNullOrBlank()) {
            Spacer(modifier = Modifier.height(8.dp))
          }
        }

        // Render Text
        if (!entry.content.isNullOrBlank()) {
          Text(
            text = entry.content,
            color = colors.second,
            style = MaterialTheme.typography.bodyLarge,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
          )
          if (entry.isTruncated) {
            TextButton(
              onClick = { if (!selectionMode) onShowMore(entry.id) else onToggleSelection() },
              contentPadding = PaddingValues(top = 8.dp, bottom = 0.dp)
            ) {
              Text(stringResource(id = R.string.action_show_more))
            }
          }
        }

        if (isGroupTail) {
          Row(
            modifier = Modifier.padding(top = 4.dp, end = 4.dp, bottom = 4.dp),
            verticalAlignment = Alignment.CenterVertically
          ) {
            Text(
              text = formatTimestamp(entry.timestamp, zoneId, formatters),
              style = MaterialTheme.typography.labelSmall,
              color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
            )
            if (isFromMe) {
              Icon(
                imageVector = when (entry.status) {
                  MessageStatus.Read -> Icons.Filled.DoneAll
                  MessageStatus.Delivered -> Icons.Filled.Done
                  MessageStatus.Sending -> Icons.Filled.Schedule
                  else -> Icons.Filled.Schedule
                },
                contentDescription = stringResource(
                  id = R.string.message_status_description,
                  entry.status
                ),
                modifier = Modifier
                  .size(16.dp)
                  .padding(start = 4.dp),
                tint = if (entry.status == MessageStatus.Read) {
                  MaterialTheme.extendedColors.success
                } else {
                  MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                }
              )
            }
          }
        }
      }
    }

    if (!isFromMe) {
      Row(verticalAlignment = Alignment.CenterVertically) {
        bubbleContent()
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
    } else {
      bubbleContent()
    }
  }
}

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

@Serializable
private data class JsonAction(val action: String)

@Composable
fun SystemMessageBubble(
  entry: HistoryEntry,
  onClick: (HistoryEntry) -> Unit,
  isUnread: Boolean = false
) {
  val parsedAction = remember(entry.content, entry.isTruncated) {
    entry.content?.let { content ->
      try {
        jsonParser.decodeFromString<JsonAction>(content)
      } catch (e: Exception) {
        val actionMatch = Regex("\"action\"\\s*:\\s*\"([^\"]+)\"").find(content)
        actionMatch?.groupValues?.get(1)?.let { action -> JsonAction(action) }
      }
    }
  }

  val textResId = LocalizationMapping.getSystemMessageResId(parsedAction?.action)

  val finalResId =
    if (parsedAction?.action == "contact_confirm" && entry.direction == MessageDirection.Sent) {
      textResId
    } else {
      textResId
    }

  if (parsedAction == null && entry.content.isNullOrBlank()) return

  val isClickable = parsedAction?.action == "profile_update_diff"

  Row(
    modifier = Modifier
      .fillMaxWidth()
      .padding(vertical = 8.dp)
      .clip(MaterialTheme.shapes.small)
      .clickable(enabled = isClickable) { onClick(entry) }
      .padding(horizontal = 16.dp),
    horizontalArrangement = Arrangement.Center,
    verticalAlignment = Alignment.CenterVertically
  ) {
    Box(
      modifier = Modifier
        .clip(RoundedCornerShape(12.dp))
        .background(MaterialTheme.colorScheme.surfaceContainer)
        .padding(horizontal = 12.dp, vertical = 6.dp)
    ) {
      Text(
        text = stringResource(id = finalResId),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
    }
    AnimatedVisibility(
      visible = isUnread,
      enter = scaleIn() + fadeIn(),
      exit = scaleOut() + fadeOut()
    ) {
      Box(
        modifier = Modifier
          .padding(start = 8.dp)
          .size(6.dp)
          .background(MaterialTheme.colorScheme.primary, CircleShape)
      )
    }
  }
}

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
