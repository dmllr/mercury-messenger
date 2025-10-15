package hela.eillance.ui.peers

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import hela.eillance.ndkio.Peer

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PeerListScreen(
  peers: List<Peer>,
  onPeerClick: (String) -> Unit,
  onRefresh: () -> Unit
) {
  Scaffold(
    topBar = { TopAppBar(title = { Text("Peers") }) },
    floatingActionButton = {
      FloatingActionButton(onClick = onRefresh) {
        Icon(Icons.Default.Refresh, contentDescription = "Refresh")
      }
    }
  ) { padding ->
    if (peers.isEmpty()) {
      Box(modifier = Modifier.fillMaxSize().padding(padding)) {
        Text("No peers found. Refresh to discover.")
      }
    } else {
      LazyColumn(modifier = Modifier.padding(padding)) {
        items(peers) { peer ->
          ListItem(
            headlineContent = { Text(peer.name) },
            supportingContent = { Text(peer.id) },
            modifier = Modifier.clickable { onPeerClick(peer.id) }
          )
        }
      }
    }
  }
}
