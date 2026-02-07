package hela.eillance.screens

import android.widget.Toast
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import hela.eillance.camera.R
import hela.eillance.navigation.Screen
import hela.eillance.viewmodels.SettingsViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
  settingsVM: SettingsViewModel,
  onBack: () -> Unit,
  navController: NavController
) {
  val context = LocalContext.current
  val signalingUrl by settingsVM.signalingUrl.collectAsState()
  val peerInactivityTimeout by settingsVM.peerInactivityTimeout.collectAsState()
  val showNotificationContent by settingsVM.showNotificationContent.collectAsState()

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_settings)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(id = R.string.action_back))
          }
        }
      )
    }
  ) { padding ->
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
        .padding(16.dp)
    ) {
      // Group Title
      Text(
        stringResource(id = R.string.settings_group_connection),
        style = MaterialTheme.typography.titleMedium,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(bottom = 8.dp)
      )

      // Setting Item: Custom Server URL
      OutlinedTextField(
        value = signalingUrl,
        onValueChange = settingsVM::onSignalingUrlChange,
        label = { Text(stringResource(id = R.string.settings_server_url_label)) },
        placeholder = { Text(stringResource(id = R.string.settings_server_url_placeholder)) },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true
      )
      Spacer(modifier = Modifier.height(4.dp))
      Text(
        stringResource(id = R.string.settings_server_url_description),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )

      Spacer(modifier = Modifier.height(16.dp))

      // Setting Item: Peer Inactivity Timeout
      OutlinedTextField(
        value = peerInactivityTimeout.toString(),
        onValueChange = {
          if (it.isEmpty()) {
            settingsVM.onPeerInactivityTimeoutChange(0)
          } else {
            it.toIntOrNull()?.let { num ->
              settingsVM.onPeerInactivityTimeoutChange(num)
            }
          }
        },
        label = { Text(stringResource(id = R.string.settings_peer_timeout_label)) },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number)
      )
      Spacer(modifier = Modifier.height(4.dp))
      Text(
        stringResource(id = R.string.settings_peer_timeout_description),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )

      HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp))

      Text(
        stringResource(id = R.string.settings_group_notifications),
        style = MaterialTheme.typography.titleMedium,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(bottom = 8.dp)
      )

      ListItem(
        headlineContent = { Text(stringResource(id = R.string.settings_show_content_title)) },
        supportingContent = { Text(stringResource(id = R.string.settings_show_content_description)) },
        trailingContent = {
          Switch(checked = showNotificationContent, onCheckedChange = settingsVM::onShowNotificationContentChange)
        }
      )

      HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp))

      Text(
        stringResource(id = R.string.settings_group_privacy),
        style = MaterialTheme.typography.titleMedium,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(bottom = 8.dp)
      )

      ListItem(
        headlineContent = { Text(stringResource(id = R.string.settings_blocked_contacts_title)) },
        supportingContent = { Text(stringResource(id = R.string.settings_blocked_contacts_description)) },
        modifier = Modifier
          .clip(MaterialTheme.shapes.medium)
          .clickable { navController.navigate(Screen.RejectedContacts.route) }
      )

      // This spacer pushes the save button to the bottom if the content is short.
      Spacer(modifier = Modifier.weight(1f))

      // Action Button
      Button(
        onClick = {
          settingsVM.saveSettings()
          Toast.makeText(context, context.getString(R.string.settings_saved_toast), Toast.LENGTH_SHORT).show()
        },
        modifier = Modifier.align(Alignment.End)
      ) {
        Text(stringResource(id = R.string.action_save))
      }
    }
  }
}
