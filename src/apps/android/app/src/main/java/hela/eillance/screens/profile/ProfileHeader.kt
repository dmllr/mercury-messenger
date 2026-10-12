package hela.eillance.screens.profile

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PermIdentity
import androidx.compose.material.icons.filled.QrCode2
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.theme.ListItem
import hela.eillance.theme.extendedColors
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.SettingsViewModel

@Composable
fun ProfileHeader(
  myProfile: MyProfileInfo,
  contactsVM: ContactsViewModel,
  settingsVM: SettingsViewModel
) {
  val context = LocalContext.current
  val connectionState by settingsVM.connectionState.collectAsState()
  val bootstrapNodeUrl by settingsVM.bootstrapNodeUrl.collectAsState()
  val networkStats by settingsVM.networkStats.collectAsState()

  var showQrDialog by remember { mutableStateOf(false) }
  var expandedNerdStats by remember { mutableStateOf(false) }
  val clipboardManager = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager

  val copyIdToClipboard: () -> Unit = {
    clipboardManager.setPrimaryClip(
      ClipData.newPlainText(
        context.getString(R.string.profile_my_id_title),
        myProfile.myId
      )
    )
    Toast.makeText(
      context,
      context.getString(R.string.profile_id_copied_toast),
      Toast.LENGTH_SHORT
    ).show()
  }

  Column(
    modifier = Modifier
      .fillMaxWidth()
      .padding(top = 24.dp),
    horizontalAlignment = Alignment.CenterHorizontally
  ) {
    if (showQrDialog) {
      QrCodeDialog(
        profileId = myProfile.myId,
        contactsVM = contactsVM,
        onDismiss = { showQrDialog = false }
      )
    }

    Box(contentAlignment = Alignment.Center) {
      UserAvatar(
        photoBase64 = myProfile.myPhoto,
        modifier = Modifier.size(120.dp),
        fallbackBackground = MaterialTheme.colorScheme.surfaceVariant,
        fallbackTint = MaterialTheme.colorScheme.onSurfaceVariant
      )

      IconButton(
        onClick = { showQrDialog = true },
        modifier = Modifier
          .align(Alignment.BottomEnd)
          .size(48.dp)
          .clip(CircleShape)
          .background(MaterialTheme.colorScheme.secondaryContainer)
          .border(2.dp, MaterialTheme.colorScheme.background, CircleShape)
      ) {
        Icon(
          Icons.Default.QrCode2,
          contentDescription = "Show QR Code",
          tint = MaterialTheme.colorScheme.onSecondaryContainer
        )
      }
    }

    Spacer(modifier = Modifier.height(16.dp))

    if (!myProfile.myDisplayName.isNullOrBlank()) {
      Text(
        text = myProfile.myDisplayName,
        style = MaterialTheme.typography.headlineSmall,
        textAlign = TextAlign.Center
      )
      Spacer(modifier = Modifier.height(4.dp))
    }

    if (!myProfile.myBio.isNullOrBlank()) {
      Text(
        text = myProfile.myBio,
        style = MaterialTheme.typography.bodyLarge,
        textAlign = TextAlign.Center,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
      Spacer(modifier = Modifier.height(16.dp))
    }

    val isOnline = connectionState == ServiceConnectionState.Online
    val (statusText, statusColor) = if (!isOnline) {
      when (connectionState) {
        ServiceConnectionState.Establishing -> stringResource(id = R.string.service_connection_state_establishing) to MaterialTheme.extendedColors.warning
        ServiceConnectionState.NoNetwork -> stringResource(id = R.string.status_no_network) to MaterialTheme.colorScheme.error
        ServiceConnectionState.Failure -> stringResource(id = R.string.status_bootstrap_error) to MaterialTheme.colorScheme.error
        else -> stringResource(id = R.string.network_offline) to MaterialTheme.colorScheme.onSurfaceVariant
      }
    } else {
      // Degraded only applies if completely isolated. If we have at least 1 peer, we are healthy enough to communicate.
      if (networkStats.bootstrapNodesAlive > 0 || networkStats.connectedPeers > 0) {
        stringResource(id = R.string.network_healthy) to MaterialTheme.extendedColors.success
      } else {
        stringResource(id = R.string.network_degraded) to MaterialTheme.extendedColors.warning
      }
    }

    Surface(
      color = statusColor.copy(alpha = 0.1f),
      shape = MaterialTheme.shapes.small,
      modifier = Modifier.padding(bottom = 16.dp).clip(MaterialTheme.shapes.small).clickable { expandedNerdStats = !expandedNerdStats }
    ) {
      Column(modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          Box(
            modifier = Modifier
              .size(8.dp)
              .background(statusColor, CircleShape)
          )
          Spacer(Modifier.width(8.dp))
          Column {
            Text(text = statusText, style = MaterialTheme.typography.labelLarge, color = statusColor)
            if (connectionState == ServiceConnectionState.Failure && bootstrapNodeUrl.isNotEmpty()) {
              Text(
                text = stringResource(id = R.string.status_bootstrap_url, bootstrapNodeUrl),
                style = MaterialTheme.typography.labelSmall,
                color = statusColor
              )
            }
          }
        }
        if (expandedNerdStats) {
          Spacer(Modifier.height(8.dp))
          androidx.compose.material3.HorizontalDivider(color = statusColor.copy(alpha = 0.3f))
          Spacer(Modifier.height(8.dp))
          Text(stringResource(R.string.nerd_stats_title), style = MaterialTheme.typography.labelMedium, color = statusColor)
          Spacer(Modifier.height(4.dp))
          Text(stringResource(R.string.stats_bootstrap_nodes, networkStats.bootstrapNodesAlive), style = MaterialTheme.typography.bodySmall, color = statusColor)
          Text(stringResource(R.string.stats_connected_nodes, networkStats.connectedPeers), style = MaterialTheme.typography.bodySmall, color = statusColor)
          Text(stringResource(R.string.stats_dht_peers, networkStats.dhtPeers), style = MaterialTheme.typography.bodySmall, color = statusColor)

          val roleStr = when (networkStats.deviceRole) {
            "turn_server" -> stringResource(R.string.role_turn_server)
            "relay_node" -> stringResource(R.string.role_relay_node)
            else -> stringResource(R.string.role_simple_node)
          }
          Text(stringResource(R.string.stats_device_role, roleStr), style = MaterialTheme.typography.bodySmall, color = statusColor)
        }
      }
    }

    Row(modifier = Modifier.padding(horizontal = 8.dp)) {
      ListItem(
        headlineContent = { Text(stringResource(id = R.string.profile_my_id_title)) },
        supportingContent = { Text(formatId(myProfile.myId)) },
        leadingContent = {
          Icon(
            Icons.Default.PermIdentity,
            contentDescription = stringResource(id = R.string.profile_my_id_title)
          )
        },
        modifier = Modifier
          .clip(MaterialTheme.shapes.medium)
          .clickable(onClick = copyIdToClipboard)
      )
    }
  }
}

private fun formatId(id: String): String {
  if (id.isEmpty()) return ""
  return id.chunked(4).joinToString(" ")
}
