package hela.eillance.components

import android.content.Context
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Done
import androidx.compose.material.icons.filled.DoneAll
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material3.Badge
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.localization.LocalizationMapping
import hela.eillance.ndkio.AttachmentType
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallStatus
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.MessageStatus
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.theme.ListItem
import hela.eillance.theme.extendedColors
import org.json.JSONObject
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ContactListItem(
  contact: Contact,
  onClick: () -> Unit,
  onShowProfile: () -> Unit,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>
) {
  val context = LocalContext.current
  val containerColor = if (contact.status == ContactStatus.awaitingApproval) {
    MaterialTheme.colorScheme.tertiaryContainer
  } else {
    Color.Transparent
  }

  val displayName = contact.name.ifEmpty {
    contact.peerId.take(8)
  }

  val timestampText = if (contact.lastActivityTimestamp > 0) {
    formatContactTimestamp(contact.lastActivityTimestamp, zoneId, formatters, context)
  } else {
    ""
  }

  ListItem(
    leadingContent = {
      UserAvatar(
        photoBase64 = contact.photo,
        modifier = Modifier.size(48.dp),
        onClick = onShowProfile
      )
    },
    headlineContent = {
      Text(
        text = displayName,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        style = MaterialTheme.typography.bodyLarge,
        fontWeight = FontWeight.SemiBold
      )
    },
    supportingContent = {
      if (contact.status == ContactStatus.awaitingApproval) {
        Column {
          Text(
            text = stringResource(R.string.sys_contact_req),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.tertiary
          )
          if (!contact.bio.isNullOrBlank()) {
            Text(
              text = contact.bio,
              style = MaterialTheme.typography.bodySmall,
              maxLines = 1,
              overflow = TextOverflow.Ellipsis
            )
          }
        }
      } else {
        val lastKind = contact.lastMsgKindEnum

        if (lastKind == HistoryEntryKind.VoiceCall || lastKind == HistoryEntryKind.VideoCall) {
          val direction =
            if (contact.lastMessageDirection == 0) CallDirection.Outgoing else CallDirection.Incoming
          val status = CallStatus.fromInt(contact.lastMessageStatus)

          CallEntryDisplay(
            kind = lastKind,
            direction = direction,
            status = status,
            durationSeconds = contact.lastMessageDuration,
            textStyle = MaterialTheme.typography.bodyMedium,
            baseColor = MaterialTheme.colorScheme.onSurfaceVariant
          )
        } else {
          val snippet = getSnippetText(contact, context)
          if (snippet.isNotBlank()) {
            Row(verticalAlignment = Alignment.CenterVertically) {
              if (contact.lastMessageDirection == MessageDirection.Sent.value &&
                lastKind == HistoryEntryKind.TextMessage
              ) {
                val statusIcon = when (contact.lastMsgStatusEnum) {
                  MessageStatus.Read -> Icons.Filled.DoneAll
                  MessageStatus.Delivered -> Icons.Filled.Done
                  MessageStatus.Sending -> Icons.Filled.Schedule
                  else -> Icons.Filled.Schedule
                }
                val statusTint = if (contact.lastMsgStatusEnum == MessageStatus.Read) {
                  MaterialTheme.extendedColors.success
                } else {
                  MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                }

                Icon(
                  imageVector = statusIcon,
                  contentDescription = null,
                  modifier = Modifier
                    .size(16.dp)
                    .padding(end = 4.dp),
                  tint = statusTint
                )
              }

              Text(
                text = snippet,
                style = MaterialTheme.typography.bodyMedium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                color = MaterialTheme.colorScheme.onSurfaceVariant
              )
            }
          } else if (!contact.bio.isNullOrBlank()) {
            Text(
              text = contact.bio,
              style = MaterialTheme.typography.bodyMedium,
              maxLines = 1,
              overflow = TextOverflow.Ellipsis,
              color = MaterialTheme.colorScheme.onSurfaceVariant
            )
          }
        }
      }
    },
    trailingContent = {
      Column(horizontalAlignment = Alignment.End) {
        if (timestampText.isNotEmpty()) {
          Text(
            text = timestampText,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
          )
        }
        if (contact.unreadCount > 0) {
          Badge { Text(contact.unreadCount.toString()) }
        } else if (contact.connectionState == PeerConnectionState.Connected.value) {
          Box(
            modifier = Modifier
              .size(8.dp)
              .background(MaterialTheme.extendedColors.success, CircleShape)
          )
        }
      }
    },
    modifier = Modifier.clickable(onClick = onClick),
    colors = ListItemDefaults.colors(containerColor = containerColor)
  )
}

fun formatContactTimestamp(
  timestamp: Long,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>,
  context: Context
): String {
  val messageTime = Instant.ofEpochMilli(timestamp / 1000).atZone(zoneId)
  val now = ZonedDateTime.now(zoneId)
  val isToday = messageTime.toLocalDate().isEqual(now.toLocalDate())
  val isYesterday = messageTime.toLocalDate().isEqual(now.toLocalDate().minusDays(1))

  return when {
    isToday -> messageTime.format(formatters.first)
    isYesterday -> context.getString(R.string.time_yesterday)
    else -> messageTime.format(formatters.second)
  }
}

private fun getSnippetText(contact: Contact, context: Context): String {
  val kind = contact.lastMsgKindEnum ?: return ""
  return when (kind) {
    HistoryEntryKind.TextMessage -> {
      val content = contact.lastMessageContent
      if (!content.isNullOrBlank()) {
        content
      } else if (contact.lastMessageAttachmentType != -1) {
        val typeText = when (contact.lastMessageAttachmentType) {
          AttachmentType.Image.value -> context.getString(R.string.file_type_image)
          AttachmentType.Video.value -> context.getString(R.string.file_type_video)
          else -> context.getString(R.string.file_type_generic)
        }
        "[$typeText]"
      } else {
        ""
      }
    }

    HistoryEntryKind.SystemMessage -> {
      val action = try {
        val content = contact.lastMessageContent
        if (content.isNullOrBlank()) null
        else JSONObject(content).optString("action")
      } catch (_: Exception) {
        val content = contact.lastMessageContent
        if (!content.isNullOrBlank()) {
          Regex("\"action\"\\s*:\\s*\"([^\"]+)\"").find(content)?.groupValues?.get(1)
        } else null
      }
      context.getString(LocalizationMapping.getSystemMessageResId(action))
    }

    else -> ""
  }
}
