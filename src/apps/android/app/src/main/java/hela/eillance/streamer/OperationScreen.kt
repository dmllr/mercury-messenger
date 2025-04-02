package hela.eillance.streamer

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.PeerConnectionState
import kotlinx.coroutines.delay

/**
 * Display operation screen with connected clients
 */
@Composable
fun OperationScreen(viewModel: StreamerViewModel) {
  val peerConnections by viewModel.peerConnections.collectAsState()
  val peerPingTimestamps by viewModel.peerPingTimestamps.collectAsState()

  Column(
    modifier = Modifier
      .fillMaxSize()
  ) {
    // Connected Clients Section heading
    Text(
      text = stringResource(id = R.string.fr_operation_connected_clients_title),
      style = MaterialTheme.typography.titleMedium,
      modifier = Modifier.padding(start = 12.dp)
    )

    Spacer(modifier = Modifier.height(8.dp))

    // List of connected clients
    LazyColumn(
      modifier = Modifier
        .fillMaxWidth()
        .weight(1f)
    ) {
      items(peerConnections.keys.toList()) { peerId ->
        PeerItemCard(
          peerId = peerId,
          states = peerConnections[peerId] ?: emptyMap(),
          lastPingTimestamp = peerPingTimestamps[peerId],
          modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp)
        )
      }

      // Show empty state when no peers are connected
      if (peerConnections.isEmpty()) {
        item {
          Text(
            text = stringResource(id = R.string.fr_operation_no_connected_clients),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier
              .fillMaxWidth()
              .padding(16.dp),
            color = MaterialTheme.colorScheme.onSurfaceVariant
          )
        }
      }
    }
  }
}

/**
 * Card displaying an individual peer connection
 */
@Composable
fun PeerItemCard(
  peerId: String,
  states: Map<EventSource, Int>,
  lastPingTimestamp: Long?,
  modifier: Modifier = Modifier
) {
  val context = LocalContext.current
  var pingColor by remember { mutableStateOf(Color.Transparent) }
  var lastProcessedTimestamp by remember { mutableStateOf(0L) }

  // ping indicator flash effect
  LaunchedEffect(lastPingTimestamp) {
    if (lastPingTimestamp != null && lastPingTimestamp != lastProcessedTimestamp) {
      lastProcessedTimestamp = lastPingTimestamp
      pingColor = Color.Green
      delay(333)
      pingColor = Color.Transparent
    }
  }

  Card(
    modifier = modifier,
    elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
  ) {
    Row(
      modifier = Modifier
        .fillMaxWidth()
        .padding(8.dp),
      verticalAlignment = Alignment.CenterVertically
    ) {
      Text(
        text = peerId,
        style = hela.eillance.theme.Typography.bodyMedium,
        modifier = Modifier.weight(1f)
      )

      // Ping indicator
      Text(
        text = stringResource(id = R.string.led),
        color = pingColor,
        modifier = Modifier.padding(horizontal = 4.dp)
      )

      // Peer connection state
      states[EventSource.PeerConnectionState]?.let { stateValue ->
        Text(
          text = PeerConnectionState.fromInt(stateValue).getResourceString(context),
          modifier = Modifier.padding(start = 4.dp)
        )
      }
    }
  }
}
