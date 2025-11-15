package hela.eillance.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import hela.eillance.navigation.Route

private const val URL_IDENTITY = "https://mercury.eillance.com/articles/cryptographic-identity/embed/"
private const val URL_P2P = "https://mercury.eillance.com/articles/peer-to-peer-connections/embed/"
private const val URL_E2EE = "https://mercury.eillance.com/articles/end-to-end-encryption/embed/"

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HelpScreen(navController: NavController) {
  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text("Help") },
        navigationIcon = {
          IconButton(onClick = { navController.popBackStack() }) {
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
        .padding(8.dp)
    ) {
      HelpItem(title = "What is a Cryptographic Identity?") { navController.navigate(Route.WebView.createRoute(URL_IDENTITY)) }
      HelpItem(title = "The Power of Peer-to-Peer") { navController.navigate(Route.WebView.createRoute(URL_P2P)) }
      HelpItem(title = "Securing the Conversation: End-to-End Encryption") {
        navController.navigate(Route.WebView.createRoute(URL_E2EE))
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
