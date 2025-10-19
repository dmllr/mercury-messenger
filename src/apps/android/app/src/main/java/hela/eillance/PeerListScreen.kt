package hela.eillance

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import hela.eillance.ndkio.Peer

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PeerListScreen(
  peers: List<Peer>,
  viewModel: MainViewModel,
  onPeerClick: (String) -> Unit
) {
  val showDialog by viewModel.showAddContactDialog.collectAsState()

  if (showDialog) {
    AddContactDialog(
      onDismissRequest = { viewModel.onAddContactDialogDismiss() },
      onConfirm = { peerId -> viewModel.addContact(peerId) }
    )
  }

  Scaffold(
    topBar = { TopAppBar(title = { Text("Mercury Peers") }) },
    floatingActionButton = {
      FloatingActionButton(onClick = { viewModel.onAddContactClicked() }) {
        Icon(Icons.Default.Add, contentDescription = "Add Contact")
      }
    }
  ) { padding ->
    Box(modifier = Modifier
      .fillMaxSize()
      .padding(padding)) {
      if (peers.isEmpty()) {
        Column(
          modifier = Modifier.fillMaxSize(),
          verticalArrangement = Arrangement.Center,
          horizontalAlignment = Alignment.CenterHorizontally
        ) {
          Text(
            "No online peers found.",
            textAlign = TextAlign.Center
          )
          Text(
            "Add a contact to get started.",
            textAlign = TextAlign.Center
          )
        }
      } else {
        LazyColumn {
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
}
