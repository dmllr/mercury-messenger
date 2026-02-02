package hela.eillance.screens

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
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
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.core.os.ConfigurationCompat
import hela.eillance.BannerType
import hela.eillance.MainViewModel
import hela.eillance.camera.R
import hela.eillance.components.StatusSnackbar
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.theme.extendedColors
import hela.eillance.ui.LocalizationMapping
import hela.eillance.viewmodels.ContactsViewModel
import org.json.JSONObject
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale

@SuppressLint("BatteryLife")
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(
  contactsVM: ContactsViewModel,
  mainViewModel: MainViewModel,
  myProfile: MyProfileInfo,
  contacts: List<Contact>,
  isLoading: Boolean,
  connectionState: ServiceConnectionState,
  onCheckPermissions: () -> Unit,
  onContactClick: (String) -> Unit,
  onNavigateToProfile: () -> Unit,
  onShowContactProfile: (String) -> Unit
) {
  val snackbarHostState = remember { SnackbarHostState() }
  val context = LocalContext.current
  val bannerState by mainViewModel.permissionBannerState.collectAsState()
  val hasPermissionIssues by mainViewModel.hasPermissionIssues.collectAsState()

  // State for permissions launchers
  val notifLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { onCheckPermissions() }
  val callLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { onCheckPermissions() }

  LaunchedEffect(contactsVM) {
    contactsVM.errorMessage.collect { message ->
      snackbarHostState.showSnackbar(message)
    }
  }

  Scaffold(
    snackbarHost = { SnackbarHost(snackbarHostState) },
    topBar = {
      TopAppBar(
        title = {
          Box {
            IconButton(onClick = onNavigateToProfile) {
              Box {
                if (!myProfile.myPhoto.isNullOrBlank()) {
                  UserAvatar(
                    photoBase64 = myProfile.myPhoto,
                    modifier = Modifier.size(32.dp)
                  )
                } else {
                  Icon(Icons.Default.Person, contentDescription = "Profile")
                }
              }
            }

            if (hasPermissionIssues) {
              Box(
                modifier = Modifier
                  .align(Alignment.TopEnd)
                  .padding(top = 8.dp, end = 8.dp)
                  .size(10.dp)
                  .background(MaterialTheme.colorScheme.error, CircleShape)
                  .border(1.dp, MaterialTheme.colorScheme.surface, CircleShape)
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
      FloatingActionButton(
        onClick = { contactsVM.onAddContactClicked() }
      ) {
        Icon(Icons.Default.Add, contentDescription = "Add Contact")
      }
    }
  ) { padding ->
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
    ) {
      // Logic for banner display
      // 1. Connection issues take priority
      val isConnectionIssue = connectionState != ServiceConnectionState.Online &&
          connectionState != ServiceConnectionState.Establishing &&
          connectionState != ServiceConnectionState.Unknown

      // 2. Permission banners only if connection is OK
      val showPermissionBanner = !isConnectionIssue && bannerState != null

      AnimatedVisibility(
        visible = isConnectionIssue || showPermissionBanner,
        enter = expandVertically(expandFrom = Alignment.Top) + fadeIn(),
        exit = shrinkVertically(shrinkTowards = Alignment.Top) + fadeOut()
      ) {
        Box(modifier = Modifier.padding(16.dp)) {
          if (isConnectionIssue) {
            val (msg, isError) = when (connectionState) {
              ServiceConnectionState.NoNetwork -> context.getString(R.string.connection_no_network) to false
              ServiceConnectionState.Failure -> context.getString(R.string.connection_failed) to true
              ServiceConnectionState.Disconnected -> context.getString(R.string.connection_disconnected) to false
              ServiceConnectionState.InvalidToken -> context.getString(R.string.connection_auth_error) to true
              else -> context.getString(R.string.connection_issue) to true
            }
            StatusSnackbar(text = msg, isError = isError)
          } else {
            when (bannerState) {
              BannerType.Notification -> {
                StatusSnackbar(
                  text = "Notifications are disabled",
                  actionLabel = "Fix it",
                  onAction = {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                      notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
                    } else {
                      openAppSettings(context)
                    }
                  }
                )
              }
              BannerType.Battery -> {
                StatusSnackbar(
                  text = "Battery optimization may delay messages",
                  actionLabel = "Fix it",
                  onAction = {
                    val intent = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                      data = "package:${context.packageName}".toUri()
                    }
                    try {
                      context.startActivity(intent)
                    } catch (e: Exception) {
                      openAppSettings(context)
                    }
                  },
                  dismissLabel = "I don't care",
                  onDismiss = { mainViewModel.ignoreBatteryOptimization() }
                )
              }
              BannerType.CallPermissions -> {
                StatusSnackbar(
                  text = "Camera/Mic needed for calls",
                  actionLabel = "Fix it",
                  onAction = {
                    callLauncher.launch(arrayOf(
                      Manifest.permission.CAMERA,
                      Manifest.permission.RECORD_AUDIO,
                      Manifest.permission.READ_PHONE_STATE,
                      Manifest.permission.MANAGE_OWN_CALLS
                    ))
                  },
                  dismissLabel = "I don't care",
                  onDismiss = { mainViewModel.ignoreCallPermissions() }
                )
              }
              null -> {}
            }
          }
        }
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
            context.getString(R.string.contacts_empty_message),
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

  val displayName = if (contact.name.isNotEmpty()) {
    contact.name
  } else {
    contact.peerId.take(8)
  }

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
    isToday -> messageTime.format(formatters.first)
    isYesterday -> "Yesterday"
    else -> messageTime.format(formatters.second)
  }
}

private fun openAppSettings(context: Context) {
  val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
    data = Uri.fromParts("package", context.packageName, null)
    flags = Intent.FLAG_ACTIVITY_NEW_TASK
  }
  context.startActivity(intent)
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
            context.getString(LocalizationMapping.getCallDescriptionResId(kind, contact.lastMessageDirection))
        }
        HistoryEntryKind.SystemMessage -> {
             try {
                 val json = JSONObject(contact.lastMessageContent ?: "{}")
                 val action = json.optString("action")
                 context.getString(LocalizationMapping.getSystemMessageResId(action))
             } catch (e: Exception) {
                 context.getString(R.string.sys_default)
             }
        }
    }
}
