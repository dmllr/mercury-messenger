package hela.eillance.screens

import android.widget.Toast
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
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
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import hela.eillance.MainViewModel
import hela.eillance.navigation.Route

private const val URL_IDENTITY = "https://mercury.eillance.com/articles/3/"
private const val URL_P2P = "https://mercury.eillance.com/articles/2/"
private const val URL_E2EE = "https://mercury.eillance.com/articles/4/"

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(viewModel: MainViewModel, onBack: () -> Unit, navController: NavController) {
  val context = LocalContext.current
  val signalingUrl by viewModel.signalingUrl.collectAsState()

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text("Settings") },
        navigationIcon = {
          IconButton(onClick = onBack) {
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
        .padding(16.dp)
    ) {
      // Group Title
      Text(
        "Connection settings",
        style = MaterialTheme.typography.titleMedium,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(bottom = 8.dp)
      )

      // Setting Item: Custom Server URL
      OutlinedTextField(
        value = signalingUrl,
        onValueChange = viewModel::onSignalingUrlChange,
        label = { Text("Custom server URL") },
        placeholder = { Text("wss://example.com:8000") },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true
      )
      Spacer(modifier = Modifier.height(4.dp))
      Text(
        "Leave empty to use the default server. Reconnects automatically on save.",
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )

      HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp))

      // Help Section
      Text(
        "Help",
        style = MaterialTheme.typography.titleMedium,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(bottom = 8.dp)
      )

      HelpItem(title = "What is a Cryptographic Identity?") { navController.navigate(Route.WebView.createRoute(URL_IDENTITY)) }
      HelpItem(title = "The Power of Peer-to-Peer") { navController.navigate(Route.WebView.createRoute(URL_P2P)) }
      HelpItem(title = "Securing the Conversation: End-to-End Encryption") { navController.navigate(Route.WebView.createRoute(URL_E2EE)) }

      // This spacer pushes the save button to the bottom if the content is short.
      Spacer(modifier = Modifier.weight(1f))

      // Action Button
      Button(
        onClick = {
          viewModel.saveSettings()
          Toast.makeText(context, "Settings Saved. Reconnecting...", Toast.LENGTH_SHORT).show()
        },
        modifier = Modifier.align(Alignment.End)
      ) {
        Text("Save")
      }
    }
  }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun HelpItem(title: String, onClick: () -> Unit) {
  ListItem(
    headlineContent = { Text(title) },
    modifier = Modifier
      .clip(MaterialTheme.shapes.medium)
      .clickable(onClick = onClick)
  )
}
