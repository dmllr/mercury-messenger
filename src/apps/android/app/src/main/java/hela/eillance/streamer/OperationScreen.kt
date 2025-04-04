package hela.eillance.streamer

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.ExperimentalMaterialApi
import androidx.compose.material.pullrefresh.PullRefreshIndicator
import androidx.compose.material.pullrefresh.pullRefresh
import androidx.compose.material.pullrefresh.rememberPullRefreshState
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
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
import hela.eillance.theme.AppIcons
import hela.eillance.theme.extendedColors
import kotlinx.coroutines.delay

/**
 * Display operation screen with connected clients
 */
@OptIn(ExperimentalMaterialApi::class)
@Composable
fun OperationScreen(
  viewModel: StreamerViewModel,
  isRefreshing: Boolean = false,
  onRefresh: () -> Unit = {}
) {
  val peerConnections by viewModel.peerConnections.collectAsState()
  val peerPingTimestamps by viewModel.peerPingTimestamps.collectAsState()

  val pullRefreshState = rememberPullRefreshState(
    refreshing = isRefreshing,
    onRefresh = onRefresh
  )

  Box(
    modifier = Modifier
      .fillMaxSize()
      .pullRefresh(pullRefreshState)
  ) {
    Column(
      modifier = Modifier
        .fillMaxSize()
    ) {
      // Connected Clients Section heading
      Text(
        text = stringResource(id = R.string.fr_operation_connected_clients_title),
        style = MaterialTheme.typography.titleMedium,
        modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 8.dp)
      )

      // Show empty state when no peers are connected
      if (peerConnections.isEmpty() && !isRefreshing) {
        Box(
          modifier = Modifier.fillMaxSize(),
          contentAlignment = Alignment.Center
        ) {
          Text(
            text = stringResource(id = R.string.fr_operation_no_connected_clients),
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
          )
        }
      } else {
        // List of connected clients
        LazyColumn(
          modifier = Modifier.fillMaxSize(),
          contentPadding = PaddingValues(vertical = 8.dp)
        ) {
          items(peerConnections.keys.toList()) { peerId ->
            PeerItemCard(
              peerId = peerId,
              states = peerConnections[peerId] ?: emptyMap(),
              lastPingTimestamp = peerPingTimestamps[peerId],
              modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 6.dp)
            )
          }
        }
      }
    }

    PullRefreshIndicator(
      refreshing = isRefreshing,
      state = pullRefreshState,
      modifier = Modifier.align(Alignment.TopCenter),
    )
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
        .padding(12.dp),
      verticalAlignment = Alignment.CenterVertically
    ) {
      // Status Icon
      Icon(
        imageVector = AppIcons.Status1Standby,
        contentDescription = "Connected client",
        tint = MaterialTheme.extendedColors.success,
        modifier = Modifier.size(24.dp)
      )

      Spacer(modifier = Modifier.width(12.dp))

      // Client Information
      Column(modifier = Modifier.weight(1f)) {
        Text(
          text = peerId,
          style = MaterialTheme.typography.bodyLarge
        )

        Spacer(modifier = Modifier.height(4.dp))

        // Peer connection state
        states[EventSource.PeerConnectionState]?.let { stateValue ->
          Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
              text = PeerConnectionState.fromInt(stateValue).getResourceString(context),
              style = MaterialTheme.typography.bodyMedium,
              color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            // Ping indicator
            if (pingColor != Color.Transparent) {
              Spacer(modifier = Modifier.width(8.dp))
              Text(
                text = stringResource(id = R.string.led),
                color = pingColor,
                style = MaterialTheme.typography.bodySmall
              )
            }
          }
        }
      }
    }
  }
}
