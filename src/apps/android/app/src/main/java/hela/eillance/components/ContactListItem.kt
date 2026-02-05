package hela.eillance.components

import android.content.Context
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Badge
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.theme.extendedColors
import hela.eillance.ui.LocalizationMapping
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

  val timestampText = if (contact.last_activity_timestamp > 0) {
    formatContactTimestamp(contact.last_activity_timestamp, zoneId, formatters, context)
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
            text = context.getString(R.string.sys_contact_req),
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
        val snippet = getSnippetText(contact, context)
        val subText = if (snippet.isNotBlank()) snippet else contact.bio

        if (!subText.isNullOrBlank()) {
          Text(
            text = subText,
            style = MaterialTheme.typography.bodyMedium,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            color = MaterialTheme.colorScheme.onSurfaceVariant
          )
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
      val prefix = if (contact.lastMessageDirection == MessageDirection.Sent.value)
        context.getString(R.string.chat_you_prefix) else ""
      prefix + (contact.lastMessageContent ?: "")
    }

    HistoryEntryKind.VoiceCall, HistoryEntryKind.VideoCall -> {
      context.getString(
        LocalizationMapping.getCallDescriptionResId(
          kind,
          contact.lastMessageDirection
        )
      )
    }

    HistoryEntryKind.SystemMessage -> {
      val action = try {
        val content = contact.lastMessageContent
        if (content.isNullOrBlank()) null
        else JSONObject(content).optString("action")
      } catch (e: Exception) {
        val content = contact.lastMessageContent
        if (!content.isNullOrBlank()) {
          Regex("\"action\"\\s*:\\s*\"([^\"]+)\"").find(content)?.groupValues?.get(1)
        } else null
      }
      context.getString(LocalizationMapping.getSystemMessageResId(action))
    }
  }
}
