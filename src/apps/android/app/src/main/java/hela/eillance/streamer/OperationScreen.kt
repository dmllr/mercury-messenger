package hela.eillance.streamer

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
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
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import hela.eillance.Tune
import hela.eillance.camera.R
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.PeerStateLink
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.delay

@Composable
fun OperationScreen(streamerService: StreamerService?) {
  val context = LocalContext.current
  var connectionState by remember { mutableStateOf(ServiceConnectionState.Unknown) }
  var peerConnections by remember { mutableStateOf<Map<String, Map<EventSource, Int>>>(emptyMap()) }

  // Initialize state from service
  LaunchedEffect(streamerService) {
    streamerService?.getServiceConnectionState()?.let {
      connectionState = it
    }
  }

  // Register for broadcast updates
  DisposableEffect(Unit) {
    val receiver = object : BroadcastReceiver() {
      override fun onReceive(ctx: Context?, intent: Intent?) {
        val action = intent?.action
        when (action) {
          Tune::BroadcastActionOnStateUpdate.get() -> {
            val sourceValue = intent.getIntExtra(Tune::BroadcastActionOnStateUpdate_source.get(), 0)
            val peerId = intent.getStringExtra(Tune::BroadcastActionOnStateUpdate_peer_id.get())
            val state = intent.getIntExtra(Tune::BroadcastActionOnStateUpdate_state.get(), 0)

            val source = EventSource.fromInt(sourceValue)

            if (source == EventSource.System) {
              connectionState = ServiceConnectionState.fromInt(state)
            } else if (peerId != null) {
              val currentPeers = peerConnections.toMutableMap()
              val peerState = currentPeers[peerId]?.toMutableMap() ?: mutableMapOf()
              peerState[source] = state
              currentPeers[peerId] = peerState
              peerConnections = currentPeers
            }
          }
        }
      }
    }

    val intentFilter = IntentFilter(Tune::BroadcastActionOnStateUpdate.get())
    LocalBroadcastManager.getInstance(context).registerReceiver(receiver, intentFilter)

    onDispose {
      LocalBroadcastManager.getInstance(context).unregisterReceiver(receiver)
    }
  }

  Column(
    modifier = Modifier
      .fillMaxSize()
      .padding(16.dp)
  ) {
    // Connection Status Section
    Text(
      text = stringResource(id = R.string.fr_operation_communication_status),
      style = MaterialTheme.typography.titleMedium,
      textAlign = TextAlign.Center,
      modifier = Modifier.fillMaxWidth()
    )

    Spacer(modifier = Modifier.height(8.dp))

    Text(
      text = connectionState.getResourceString(context),
      style = MaterialTheme.typography.bodyMedium,
      textAlign = TextAlign.Center,
      modifier = Modifier.fillMaxWidth()
    )

    Spacer(modifier = Modifier.height(16.dp))

    // Connected Clients Section
    Text(
      text = stringResource(id = R.string.fr_operation_connected_clients),
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
          modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp)
        )
      }
    }
  }
}

@Composable
fun PeerItemCard(
  peerId: String,
  states: Map<EventSource, Int>,
  modifier: Modifier = Modifier
) {
  val context = LocalContext.current
  var pingColor by remember { mutableStateOf(Color.Transparent) }

  // Handle ping indicator flash effect
  LaunchedEffect(states[EventSource.Ping]) {
    states[EventSource.Ping]?.let {
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
          text = PeerStateLink.fromInt(stateValue).getResourceString(context),
          modifier = Modifier.padding(start = 4.dp)
        )
      }
    }
  }
}
