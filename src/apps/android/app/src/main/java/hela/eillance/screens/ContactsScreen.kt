package hela.eillance.screens

import android.widget.Toast
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
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
import androidx.compose.material.icons.filled.SignalWifiOff
import androidx.compose.material.icons.filled.Warning
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
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.os.ConfigurationCompat
import hela.eillance.AddContactDialog
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.theme.extendedColors
import hela.eillance.viewmodels.ContactsViewModel
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ContactsScreen(
  contactsVM: ContactsViewModel,
  myProfile: MyProfileInfo,
  contacts: List<Contact>,
  isLoading: Boolean,
  connectionState: ServiceConnectionState,
  hasCallPermissions: Boolean,
  hasNotificationPermission: Boolean,
  hasBatteryExemption: Boolean,
  onCheckPermissions: () -> Unit,
  onContactClick: (String) -> Unit,
  onNavigateToProfile: () -> Unit,
  onShowContactProfile: (String) -> Unit
) {
  val snackbarHostState = remember { SnackbarHostState() }

  LaunchedEffect(contactsVM) {
    contactsVM.errorMessage.collect { message ->
      snackbarHostState.showSnackbar(message)
    }
  }

  Scaffold(
    snackbarHost = { SnackbarHost(snackbarHostState) },
    topBar = {
      TopAppBar(
        title = { Text("Mercury") },
        actions = {
          Box {
            IconButton(onClick = onNavigateToProfile) {
              Icon(Icons.Default.Person, contentDescription = "Profile")
            }
            if (connectionState != ServiceConnectionState.Unknown) {
              val (dotColor, icon) = when (connectionState) {
                ServiceConnectionState.Online -> MaterialTheme.extendedColors.success to null
                ServiceConnectionState.Establishing -> MaterialTheme.extendedColors.warning to null
                ServiceConnectionState.NoNetwork -> Color.Transparent to Icons.Default.SignalWifiOff
                ServiceConnectionState.Failure -> MaterialTheme.colorScheme.error to Icons.Default.Warning
                else -> Color.Transparent to null
              }

              // Tooltip / Long press hint
              val context = LocalContext.current
              val tooltipText = when(connectionState) {
                ServiceConnectionState.Online -> "Connected"
                ServiceConnectionState.Establishing -> "Connecting..."
                ServiceConnectionState.NoNetwork -> "No Network Connection"
                ServiceConnectionState.Failure -> "Server Connection Failed"
                else -> "Status: ${connectionState.name}"
              }

              Box(
                modifier = Modifier
                  .align(Alignment.BottomEnd)
                  .padding(4.dp)
                  .combinedClickable(
                    onClick = {},
                    onLongClick = {
                      Toast.makeText(context, tooltipText, Toast.LENGTH_SHORT).show()
                    }
                  )
              ) {
                if (icon != null) {
                  Icon(
                    imageVector = icon,
                    contentDescription = tooltipText,
                    tint = if(connectionState == ServiceConnectionState.NoNetwork) MaterialTheme.colorScheme.outline else MaterialTheme.colorScheme.error,
                    modifier = Modifier
                      .size(14.dp)
                      .background(MaterialTheme.colorScheme.surface, CircleShape)
                      .padding(1.dp)
                  )
                } else if (connectionState == ServiceConnectionState.Online || connectionState == ServiceConnectionState.Establishing) {
                  val alpha = if (connectionState == ServiceConnectionState.Establishing) {
                    val infiniteTransition = rememberInfiniteTransition(label = "conn")
                    infiniteTransition.animateFloat(
                      initialValue = 0.3f,
                      targetValue = 1f,
                      animationSpec = infiniteRepeatable(
                        animation = tween(800),
                        repeatMode = RepeatMode.Reverse
                      ),
                      label = "alpha"
                    ).value
                  } else 1f

                  Box(
                    modifier = Modifier
                      .size(12.dp)
                      .background(color = dotColor.copy(alpha = alpha), shape = CircleShape)
                      .border(1.dp, MaterialTheme.colorScheme.surface, CircleShape)
                  )
                }
              }
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

      if (isLoading) {
        Box(
          modifier = Modifier
            .weight(1f)
            .fillMaxWidth(),
          contentAlignment = Alignment.Center
        ) {
          CircularProgressIndicator()
        }
      } else if (visibleContacts.isEmpty()) {
        Box(
          modifier = Modifier
            .weight(1f)
            .fillMaxWidth(),
          contentAlignment = Alignment.Center
        ) {
          Text(
            "No contacts yet. Add one!",
            style = MaterialTheme.typography.bodyLarge
          )
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
  val timestampText = if (contact.last_activity_timestamp > 0) {
    formatContactTimestamp(contact.last_activity_timestamp, zoneId, formatters)
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
