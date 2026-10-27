package hela.eillance.screens

import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
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
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
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
  val bootstrapNodeUrls by settingsVM.bootstrapNodeUrls.collectAsState()
  val appendBootstrapNodes by settingsVM.appendBootstrapNodes.collectAsState()
  val peerInactivityTimeout by settingsVM.peerInactivityTimeout.collectAsState()

  val customTurn by settingsVM.customTurn.collectAsState()
  val useCustomTurnOnly by settingsVM.useCustomTurnOnly.collectAsState()
  var bootstrapError by remember { mutableStateOf<String?>(null) }
  var turnError by remember { mutableStateOf<String?>(null) }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.settings_group_connection)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(
              Icons.AutoMirrored.Filled.ArrowBack,
              contentDescription = stringResource(id = R.string.action_back)
            )
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
      // Setting Item: Bootstrap Node URL
      OutlinedTextField(
        value = bootstrapNodeUrls,
        onValueChange = {
          settingsVM.onBootstrapNodeUrlsChange(it)
          bootstrapError = null
        },
        label = { Text(stringResource(id = R.string.settings_bootstrap_node_url_label)) },
        placeholder = { Text("/dns4/node101.example.com/tcp/10807") },
        modifier = Modifier.fillMaxWidth(),
        singleLine = false,
        minLines = 4,
        maxLines = 10,
        isError = bootstrapError != null,
        supportingText = if (bootstrapError != null) {
          { Text(bootstrapError!!, color = MaterialTheme.colorScheme.error) }
        } else null
      )
      Spacer(modifier = Modifier.height(4.dp))
      Text(
        stringResource(id = R.string.settings_bootstrap_node_url_description),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
      Spacer(modifier = Modifier.height(8.dp))
      Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
      ) {
        Column(modifier = Modifier.weight(1f).padding(end = 16.dp)) {
          Text(
            text = stringResource(id = R.string.settings_bootstrap_append_title),
            style = MaterialTheme.typography.bodyMedium
          )
          Text(
            text = stringResource(id = R.string.settings_bootstrap_append_description),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
          )
        }
        Switch(
          checked = appendBootstrapNodes,
          onCheckedChange = settingsVM::onAppendBootstrapNodesChange
        )
      }

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

      // Custom TURN Servers
      Text(
        stringResource(id = R.string.settings_custom_turn_title),
        style = MaterialTheme.typography.titleMedium,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(bottom = 8.dp)
      )

      OutlinedTextField(
        value = customTurn,
        onValueChange = {
          settingsVM.onCustomTurnChange(it)
          turnError = null
        },
        label = { Text(stringResource(id = R.string.settings_custom_turn_label)) },
        placeholder = { Text("user:password@turn.example.com:3478?transport=udp") },
        modifier = Modifier.fillMaxWidth(),
        singleLine = false,
        minLines = 4,
        maxLines = 10,
        isError = turnError != null,
        supportingText = if (turnError != null) {
          { Text(turnError!!, color = MaterialTheme.colorScheme.error) }
        } else null
      )
      Spacer(modifier = Modifier.height(4.dp))
      Text(
        stringResource(id = R.string.settings_custom_turn_description),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
      Spacer(modifier = Modifier.height(8.dp))
      Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
      ) {
        Column(modifier = Modifier.weight(1f).padding(end = 16.dp)) {
          Text(
            text = stringResource(id = R.string.settings_turn_in_addition_title),
            style = MaterialTheme.typography.bodyMedium
          )
          Text(
            text = stringResource(id = R.string.settings_turn_in_addition_description),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
          )
        }
        Switch(
          checked = !useCustomTurnOnly,
          onCheckedChange = { settingsVM.onUseCustomTurnOnlyChange(!it) }
        )
      }

      Spacer(modifier = Modifier.height(24.dp))

      // Action Button
      Button(
        onClick = {
          when (val result = settingsVM.saveSettings()) {
            is SettingsViewModel.SaveResult.Success -> {
              bootstrapError = null
              turnError = null
              Toast.makeText(
                context,
                context.getString(R.string.settings_saved_toast),
                Toast.LENGTH_SHORT
              ).show()
            }
            is SettingsViewModel.SaveResult.InvalidBootstrap -> {
              val firstErr = result.invalidEntries.first()
              val msg = context.getString(R.string.settings_invalid_bootstrap_url, firstErr)
              bootstrapError = msg
              Toast.makeText(context, msg, Toast.LENGTH_LONG).show()
            }
            is SettingsViewModel.SaveResult.InvalidTurn -> {
              val firstErr = result.invalidEntries.first()
              val msg = context.getString(R.string.settings_invalid_turn_url, firstErr)
              turnError = msg
              Toast.makeText(context, msg, Toast.LENGTH_LONG).show()
            }
          }
        },
        modifier = Modifier.align(Alignment.End)
      ) {
        Text(stringResource(id = R.string.action_save))
      }
    }
  }
}
