package hela.eillance.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.Badge
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DividerDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.os.ConfigurationCompat
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.theme.extendedColors
import hela.eillance.utils.rememberImageBitmapFromBase64
import hela.eillance.viewmodels.ContactsViewModel
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(
  contactsVM: ContactsViewModel,
  myProfile: MyProfileInfo,
  contacts: List<Contact>,
  connectionState: ServiceConnectionState,
  hasCallPermissions: Boolean,
  hasNotificationPermission: Boolean,
  hasBatteryExemption: Boolean,
  onCheckPermissions: () -> Unit,
  onContactClick: (String) -> Unit,
  onNavigateToProfile: () -> Unit,
  onShowContactProfile: (String) -> Unit
) {
  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text("Mercury") },
        actions = {
          Box {
            IconButton(onClick = onNavigateToProfile) {
              Icon(Icons.Default.Person, contentDescription = "Profile")
            }
            if (connectionState != ServiceConnectionState.Unknown) {
              val dotColor = when (connectionState) {
                ServiceConnectionState.Online -> MaterialTheme.extendedColors.success
                ServiceConnectionState.Failure -> MaterialTheme.colorScheme.error
                else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f)
              }
              Box(
                modifier = Modifier
                  .padding(bottom = 4.dp, end = 4.dp)
                  .size(12.dp)
                  .background(color = dotColor, shape = CircleShape)
                  .align(Alignment.BottomEnd)
              )
            }
          }
        },
        colors = TopAppBarDefaults.topAppBarColors(
          containerColor = MaterialTheme.colorScheme.primaryContainer
        )
      )
    },
    floatingActionButton = {
      FloatingActionButton(onClick = { contactsVM.onAddContactClicked() }) {
        Icon(Icons.Default.Add, contentDescription = "Add Contact")
      }
    }
  ) { padding ->
    Column(modifier = Modifier.padding(padding)) {
      if (!hasNotificationPermission) {
        NotificationPermissionBanner(onCheckPermissions)
      }

      if (!hasBatteryExemption) {
        BatteryOptimizationBanner(onCheckPermissions)
      }

      if (!hasCallPermissions) {
        CallPermissionBanner(onCheckPermissions)
      }

      val visibleContacts = contacts.filter { it.status != ContactStatus.rejected }

      if (visibleContacts.isEmpty()) {
        Box(
          modifier = Modifier
            .weight(1f)
            .fillMaxWidth(),
          contentAlignment = Alignment.Center
        ) {
          if (myProfile.myId.isEmpty()) {
            CircularProgressIndicator()
          } else {
            Text(
              "No contacts yet. Add one!",
              style = MaterialTheme.typography.bodyLarge
            )
          }
        }
      } else {
        val locale = ConfigurationCompat.getLocales(LocalConfiguration.current)[0] ?: Locale.getDefault()
        val zoneId = ZoneId.systemDefault()
        val formatters = remember(locale) {
          Triple(
            DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT).withLocale(locale),
            DateTimeFormatter.ofPattern("MMM d", locale),
            DateTimeFormatter.ofLocalizedDate(FormatStyle.SHORT).withLocale(locale)
          )
        }

        LazyColumn(modifier = Modifier.weight(1f)) {
          items(visibleContacts, key = { it.peerId }) { contact ->
            ContactListItem(
              contact = contact,
              onClick = { onContactClick(contact.peerId) },
              onShowProfile = { onShowContactProfile(contact.peerId) },
              zoneId = zoneId,
              formatters = formatters
            )
            HorizontalDivider(color = DividerDefaults.color.copy(alpha = 0.5f))
          }
        }
      }
    }
  }
}

@Composable
fun ContactListItem(
  contact: Contact,
  onClick: () -> Unit,
  onShowProfile: () -> Unit,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>
) {
  val containerColor = if (contact.status == ContactStatus.awaitingApproval) {
    MaterialTheme.colorScheme.tertiaryContainer
  } else {
    Color.Transparent
  }

  val displayName = if (contact.name.isNotEmpty()) {
    contact.name
  } else {
    // Shorten crypto ID: first 8 chars
    contact.peerId.take(8)
  }

  // Last update timestamp
  val timestampText = if (contact.lastActivityTimestamp > 0) {
    formatContactTimestamp(contact.lastActivityTimestamp, zoneId, formatters)
  } else {
    ""
  }

  ListItem(
    leadingContent = {
      val imageBitmap = rememberImageBitmapFromBase64(contact.photo)
      if (imageBitmap != null) {
        Image(
          bitmap = imageBitmap,
          contentDescription = "Profile Photo",
          modifier = Modifier
            .size(48.dp)
            .clip(CircleShape)
            .clickable { onShowProfile() },
          contentScale = ContentScale.Crop
        )
      } else {
        Icon(
          imageVector = Icons.Default.Person,
          contentDescription = "Default Profile Icon",
          modifier = Modifier
            .size(48.dp)
            .clip(CircleShape)
            .clickable { onShowProfile() }
            .background(MaterialTheme.colorScheme.secondaryContainer)
            .padding(10.dp)
        )
      }
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
            text = "New Contact Request",
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
        // Show shortened last message if available, otherwise bio or ID if name is used
        val subText = contact.lastMessageSnippet ?: contact.bio
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
          // Optionally show online indicator
          Box(modifier = Modifier.size(8.dp).background(MaterialTheme.extendedColors.success, CircleShape))
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
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>
): String {
  val messageTime = Instant.ofEpochMilli(timestamp / 1000).atZone(zoneId)
  val now = ZonedDateTime.now(zoneId)

  val isToday = messageTime.toLocalDate().isEqual(now.toLocalDate())
  val isYesterday = messageTime.toLocalDate().isEqual(now.toLocalDate().minusDays(1))

  return when {
    isToday -> messageTime.format(formatters.first) // 10:30 AM
    isYesterday -> "Yesterday"
    else -> messageTime.format(formatters.second) // Jan 5
  }
}
