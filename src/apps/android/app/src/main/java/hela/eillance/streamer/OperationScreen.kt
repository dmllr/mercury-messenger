package hela.eillance.streamer

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.ExperimentalMaterialApi
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.pullrefresh.PullRefreshIndicator
import androidx.compose.material.pullrefresh.pullRefresh
import androidx.compose.material.pullrefresh.rememberPullRefreshState
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.theme.extendedColors
import hela.eillance.theme.icons.AppIcons
import hela.eillance.theme.icons.CameraStream
import hela.eillance.theme.icons.Refresh
import hela.eillance.theme.icons.StatusDisconnected
import hela.eillance.theme.icons.StatusStandby
import hela.eillance.theme.icons.StatusUnknown
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
          contentPadding = PaddingValues(vertical = 8.dp),
          verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
          items(
            items = peerConnections.keys.toList(),
            key = { it }
          ) { peerId ->
            val states = peerConnections[peerId] ?: emptyMap()
            val lastPingTimestamp = peerPingTimestamps[peerId]

            val connectionState = remember(states) {
              val stateValue = states[EventSource.PeerConnectionState] ?: PeerConnectionState.Unknown.value
              PeerConnectionState.fromInt(stateValue)
            }

            val isDismissible = connectionState == PeerConnectionState.Disconnected ||
                connectionState == PeerConnectionState.Failed ||
                connectionState == PeerConnectionState.Closed

            SwipeToDismissContainer(
              peerId = peerId,
              isDismissible = isDismissible,
              onDismiss = { dismissedPeerId ->
                viewModel.dismissPeer(dismissedPeerId)
              },
              content = {
                PeerItemCard(
                  peerId = peerId,
                  states = states,
                  lastPingTimestamp = lastPingTimestamp,
                  modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp)
                )
              }
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
 * Swipe-to-dismiss container for peer items
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SwipeToDismissContainer(
  peerId: String,
  isDismissible: Boolean,
  onDismiss: (String) -> Unit,
  content: @Composable () -> Unit
) {
  // Track animation state locally
  var isVisible by remember { mutableStateOf(true) }

  if (!isDismissible) {
    content()
    return
  }

  val dismissState = rememberSwipeToDismissBoxState(
    confirmValueChange = { value ->
      if (value == SwipeToDismissBoxValue.StartToEnd) {
        // Mark as dismissed locally for immediate feedback
        isVisible = false

        onDismiss(peerId)
        true
      } else {
        false
      }
    }
  )

  if (isVisible) {
    SwipeToDismissBox(
      state = dismissState,
      backgroundContent = {
        if (dismissState.currentValue == SwipeToDismissBoxValue.StartToEnd ||
          dismissState.targetValue == SwipeToDismissBoxValue.StartToEnd) {
          Row(
            modifier = Modifier
              .fillMaxSize()
              .background(MaterialTheme.colorScheme.errorContainer)
              .padding(horizontal = 16.dp),
            verticalAlignment = Alignment.CenterVertically
          ) {
            Icon(
              imageVector = Icons.Default.Delete,
              contentDescription = "Delete peer",
              tint = MaterialTheme.colorScheme.onErrorContainer
            )
          }
        }
      },
      enableDismissFromEndToStart = false,
      content = { content() }
    )
  }
}

@Composable
fun ConnectionStatusIcon(
  connectionState: PeerConnectionState,
  isReceivingPing: Boolean,
  modifier: Modifier = Modifier
) {
  var pingAnimation by remember { mutableStateOf(false) }

  // Handle ping animation
  LaunchedEffect(isReceivingPing) {
    if (isReceivingPing) {
      pingAnimation = true
      delay(300)
      pingAnimation = false
    }
  }

  // Rotation animation for connecting state
  val infiniteTransition = rememberInfiniteTransition(label = "connecting_rotation")
  val rotation by infiniteTransition.animateFloat(
    initialValue = 0f,
    targetValue = 360f,
    animationSpec = infiniteRepeatable(
      animation = tween(1000, easing = LinearEasing),
      repeatMode = RepeatMode.Restart
    ),
    label = "rotation_animation"
  )

  Box(modifier = modifier, contentAlignment = Alignment.Center) {
    when (connectionState) {
      PeerConnectionState.Connecting -> {
        Icon(
          imageVector = AppIcons.Refresh,
          contentDescription = "Connecting",
          tint = MaterialTheme.colorScheme.primary,
          modifier = Modifier
            .size(24.dp)
            .rotate(rotation)
        )
      }
      PeerConnectionState.Connected -> {
        // Base connected icon
        Icon(
          imageVector = AppIcons.CameraStream,
          contentDescription = "Connected client",
          tint = if (pingAnimation) MaterialTheme.extendedColors.success else MaterialTheme.colorScheme.primary,
          modifier = Modifier.size(24.dp)
        )

        // Ping ripple effect
        if (pingAnimation) {
          val animatedSize by animateFloatAsState(
            targetValue = if (pingAnimation) 40f else 24f,
            animationSpec = tween(300, easing = FastOutSlowInEasing),
            label = "ping_ripple"
          )
          val animatedAlpha by animateFloatAsState(
            targetValue = if (pingAnimation) 0f else 1f,
            animationSpec = tween(300, easing = FastOutSlowInEasing),
            label = "ping_alpha"
          )

          Box(
            modifier = Modifier
              .size(animatedSize.dp)
              .alpha(1 - animatedAlpha)
              .background(
                color = MaterialTheme.extendedColors.success.copy(alpha = 0.3f),
                shape = CircleShape
              )
          )
        }
      }
      PeerConnectionState.Disconnected, PeerConnectionState.Failed, PeerConnectionState.Closed -> {
        Icon(
          imageVector = AppIcons.StatusDisconnected,
          contentDescription = "Disconnected client",
          tint = MaterialTheme.colorScheme.error,
          modifier = Modifier.size(24.dp)
        )
      }
      PeerConnectionState.New -> {
        Icon(
          imageVector = AppIcons.StatusStandby,
          contentDescription = "New connection",
          tint = MaterialTheme.colorScheme.secondary,
          modifier = Modifier.size(24.dp)
        )
      }
      else -> {
        Icon(
          imageVector = AppIcons.StatusUnknown,
          contentDescription = "Unknown connection state",
          tint = MaterialTheme.colorScheme.onSurfaceVariant,
          modifier = Modifier.size(24.dp)
        )
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
  var isPinging by remember { mutableStateOf(false) }
  var lastProcessedTimestamp by remember { mutableStateOf(0L) }

  val connectionState = remember(states) {
    val stateValue = states[EventSource.PeerConnectionState] ?: PeerConnectionState.Unknown.value
    PeerConnectionState.fromInt(stateValue)
  }

  // ping indicator flash effect
  LaunchedEffect(lastPingTimestamp) {
    if (lastPingTimestamp != null && lastPingTimestamp != lastProcessedTimestamp) {
      lastProcessedTimestamp = lastPingTimestamp
      isPinging = true
      delay(333)
      isPinging = false
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
      // Dynamic Status Icon based on connection state
      ConnectionStatusIcon(
        connectionState = connectionState,
        isReceivingPing = isPinging,
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
        Text(
          text = connectionState.getResourceString(context),
          style = MaterialTheme.typography.bodyMedium,
          color = MaterialTheme.colorScheme.onSurfaceVariant
        )
      }
    }
  }
}
