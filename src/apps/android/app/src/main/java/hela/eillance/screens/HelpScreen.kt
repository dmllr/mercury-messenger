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
import hela.eillance.theme.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import hela.eillance.camera.R
import hela.eillance.navigation.Route

private const val URL_IDENTITY =
  "https://mercury.eillance.com/articles/cryptographic-identity/embed/"
private const val URL_P2P = "https://mercury.eillance.com/articles/peer-to-peer-connections/embed/"
private const val URL_E2EE = "https://mercury.eillance.com/articles/end-to-end-encryption/embed/"

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HelpScreen(navController: NavController) {
  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_help)) },
        navigationIcon = {
          IconButton(onClick = { navController.popBackStack() }) {
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
        .padding(8.dp)
    ) {
      HelpItem(title = stringResource(id = R.string.help_item_identity)) {
        navController.navigate(
          Route.WebView.createRoute(
            URL_IDENTITY
          )
        )
      }
      HelpItem(title = stringResource(id = R.string.help_item_p2p)) {
        navController.navigate(
          Route.WebView.createRoute(
            URL_P2P
          )
        )
      }
      HelpItem(title = stringResource(id = R.string.help_item_e2ee)) {
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
