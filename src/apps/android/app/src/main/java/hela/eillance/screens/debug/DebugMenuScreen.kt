package hela.eillance.screens.debug

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Logout
import androidx.compose.material.icons.filled.DeleteForever
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.theme.ListItem

enum class DebugAction {
  ERASE_HISTORY,
  ERASE_CONTACTS,
  REVOKE_APPROVALS
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DebugMenuScreen(
  onNavigateBack: () -> Unit,
  onNavigateToPendingActions: () -> Unit,
  onNavigateToConnections: () -> Unit,
  onNavigateToUnread: () -> Unit,
  onDebugAction: (DebugAction) -> Unit
) {
  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_debug_menu)) },
        navigationIcon = {
          IconButton(onClick = onNavigateBack) {
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
        .verticalScroll(rememberScrollState())
    ) {
      ListItem(
        headlineContent = { Text(stringResource(id = R.string.debug_menu_connections_title)) },
        supportingContent = { Text(stringResource(id = R.string.debug_menu_connections_description)) },
        modifier = Modifier.clickable { onNavigateToConnections() }
      )
      HorizontalDivider()
      ListItem(
        headlineContent = { Text(stringResource(id = R.string.screen_title_pending_actions)) },
        supportingContent = { Text(stringResource(id = R.string.debug_menu_pending_actions_description)) },
        modifier = Modifier.clickable { onNavigateToPendingActions() }
      )
      HorizontalDivider()
      ListItem(
        headlineContent = { Text(stringResource(id = R.string.debug_menu_unread_title)) },
        supportingContent = { Text(stringResource(id = R.string.debug_menu_unread_description)) },
        modifier = Modifier.clickable { onNavigateToUnread() }
      )

      HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp))
      Text(
        stringResource(id = R.string.debug_menu_destructive_actions),
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.error,
        modifier = Modifier.padding(start = 16.dp, bottom = 8.dp)
      )

      ListItem(
        headlineContent = {
          Text(
            stringResource(id = R.string.debug_action_erase_history),
            color = MaterialTheme.colorScheme.error
          )
        },
        leadingContent = {
          Icon(
            Icons.Default.DeleteForever,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.error
          )
        },
        modifier = Modifier.clickable { onDebugAction(DebugAction.ERASE_HISTORY) }
      )
      ListItem(
        headlineContent = {
          Text(
            stringResource(id = R.string.debug_action_erase_contacts),
            color = MaterialTheme.colorScheme.error
          )
        },
        leadingContent = {
          Icon(
            Icons.Default.DeleteForever,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.error
          )
        },
        modifier = Modifier.clickable { onDebugAction(DebugAction.ERASE_CONTACTS) }
      )
      ListItem(
        headlineContent = {
          Text(
            stringResource(id = R.string.debug_action_revoke_approvals),
            color = MaterialTheme.colorScheme.error
          )
        },
        leadingContent = {
          Icon(
            Icons.AutoMirrored.Filled.Logout,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.error
          )
        },
        modifier = Modifier.clickable { onDebugAction(DebugAction.REVOKE_APPROVALS) }
      )
    }
  }
}
