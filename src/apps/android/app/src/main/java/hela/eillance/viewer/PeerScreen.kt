package hela.eillance.viewer

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Card
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import hela.eillance.ndkio.Peer

@Composable
fun PeersScreen(
  peers: List<Peer>,
  onPeerClicked: (Peer) -> Unit
) {
  LazyColumn(modifier = Modifier.fillMaxSize()) {
    items(peers) { peer ->
      PeerItem(peer = peer, onPeerClicked = onPeerClicked)
    }
  }
}

@Composable
fun PeerItem(peer: Peer, onPeerClicked: (Peer) -> Unit) {
  Card(
    modifier = Modifier
      .fillMaxWidth()
      .padding(8.dp)
      .clickable { onPeerClicked(peer) }
  ) {
    Column(modifier = Modifier.padding(16.dp)) {
      Text(text = peer.name)
    }
  }
}