package hela.eillance.screens

import android.widget.Toast
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.viewmodels.SettingsViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConnectionSettingsScreen(
  settingsVM: SettingsViewModel,
  onBack: () -> Unit
) {
  val context = LocalContext.current
  val signalingUrl by settingsVM.signalingUrl.collectAsState()
  val peerInactivityTimeout by settingsVM.peerInactivityTimeout.collectAsState()

  val customStun by settingsVM.customStun.collectAsState()
  val customTurn by settingsVM.customTurn.collectAsState()
  val customTurnUser by settingsVM.customTurnUser.collectAsState()
  val customTurnPass by settingsVM.customTurnPass.collectAsState()

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.settings_group_connection)) },
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
        .verticalScroll(rememberScrollState())
    ) {
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

      // Custom ICE Servers
      Text(
        stringResource(id = R.string.settings_custom_ice_title),
        style = MaterialTheme.typography.titleMedium,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(bottom = 8.dp)
      )

      OutlinedTextField(
        value = customStun,
        onValueChange = settingsVM::onCustomStunChange,
        label = { Text(stringResource(id = R.string.settings_custom_stun_label)) },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true
      )
      Spacer(modifier = Modifier.height(8.dp))
      OutlinedTextField(
        value = customTurn,
        onValueChange = settingsVM::onCustomTurnChange,
        label = { Text(stringResource(id = R.string.settings_custom_turn_label)) },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true
      )
      Spacer(modifier = Modifier.height(8.dp))
      OutlinedTextField(
        value = customTurnUser,
        onValueChange = settingsVM::onCustomTurnUserChange,
        label = { Text(stringResource(id = R.string.settings_custom_turn_user_label)) },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true
      )
      Spacer(modifier = Modifier.height(8.dp))
      OutlinedTextField(
        value = customTurnPass,
        onValueChange = settingsVM::onCustomTurnPassChange,
        label = { Text(stringResource(id = R.string.settings_custom_turn_pass_label)) },
        visualTransformation = PasswordVisualTransformation(),
        modifier = Modifier.fillMaxWidth(),
        singleLine = true
      )

      Spacer(modifier = Modifier.height(24.dp))

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
