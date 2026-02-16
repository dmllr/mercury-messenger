package hela.eillance.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
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
  val showNotificationContent by settingsVM.showNotificationContent.collectAsState()

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_settings)) },
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
      ListItem(
        headlineContent = { Text(stringResource(id = R.string.settings_group_connection)) },
        supportingContent = { Text(stringResource(id = R.string.settings_connection_description)) },
        leadingContent = {
          Icon(
            Icons.Default.Wifi,
            contentDescription = null
          )
        },
        modifier = Modifier
          .clip(MaterialTheme.shapes.medium)
          .clickable { navController.navigate(Screen.ConnectionSettings.route) }
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
          Switch(
            checked = showNotificationContent,
            onCheckedChange = settingsVM::onShowNotificationContentChange
          )
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
    }
  }
}
