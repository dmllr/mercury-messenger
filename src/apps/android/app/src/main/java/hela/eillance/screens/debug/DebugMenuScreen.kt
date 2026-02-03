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
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

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
        title = { Text("Debug Menu") },
        navigationIcon = {
          IconButton(onClick = onNavigateBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
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
        headlineContent = { Text("Connection Debugger") },
        supportingContent = { Text("View peer connection states and timers") },
        modifier = Modifier.clickable { onNavigateToConnections() }
      )
      HorizontalDivider()
      ListItem(
        headlineContent = { Text("Pending Actions") },
        supportingContent = { Text("View queued system actions (JSON)") },
        modifier = Modifier.clickable { onNavigateToPendingActions() }
      )
      HorizontalDivider()
      ListItem(
        headlineContent = { Text("Unread Notifications") },
        supportingContent = { Text("Check internal unread counts") },
        modifier = Modifier.clickable { onNavigateToUnread() }
      )

      HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp))
      Text(
        "Destructive Actions",
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.error,
        modifier = Modifier.padding(start = 16.dp, bottom = 8.dp)
      )

      ListItem(
        headlineContent = { Text("Erase All History", color = MaterialTheme.colorScheme.error) },
        leadingContent = { Icon(Icons.Default.DeleteForever, contentDescription = null, tint = MaterialTheme.colorScheme.error) },
        modifier = Modifier.clickable { onDebugAction(DebugAction.ERASE_HISTORY) }
      )
      ListItem(
        headlineContent = { Text("Erase All Contacts", color = MaterialTheme.colorScheme.error) },
        leadingContent = { Icon(Icons.Default.DeleteForever, contentDescription = null, tint = MaterialTheme.colorScheme.error) },
        modifier = Modifier.clickable { onDebugAction(DebugAction.ERASE_CONTACTS) }
      )
      ListItem(
        headlineContent = { Text("Revoke All Approvals", color = MaterialTheme.colorScheme.error) },
        leadingContent = { Icon(Icons.AutoMirrored.Filled.Logout, contentDescription = null, tint = MaterialTheme.colorScheme.error) },
        modifier = Modifier.clickable { onDebugAction(DebugAction.REVOKE_APPROVALS) }
      )
    }
  }
}
