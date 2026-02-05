package hela.eillance.screens.conversation

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.theme.extendedColors

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ConnectionStatusBar(contact: Contact) {
  var isExpanded by remember { mutableStateOf(false) }
  val connectionState = PeerConnectionState.fromInt(contact.connectionState)
  val context = LocalContext.current

  val (statusText, statusColor) = when (connectionState) {
    PeerConnectionState.Connected -> stringResource(R.string.state_peer_connection_connected) to MaterialTheme.extendedColors.success
    PeerConnectionState.Connecting -> stringResource(R.string.state_peer_connection_connecting) to MaterialTheme.colorScheme.secondary
    PeerConnectionState.Failed -> stringResource(R.string.state_peer_connection_failed) to MaterialTheme.colorScheme.error
    else -> stringResource(R.string.state_peer_connection_disconnected) to MaterialTheme.colorScheme.onSurface.copy(
      alpha = 0.6f
    )
  }

  val height by animateDpAsState(
    targetValue = if (isExpanded) 64.dp else 24.dp,
    label = "statusBarHeight"
  )

  Surface(
    modifier = Modifier
      .fillMaxWidth()
      .height(height)
      .combinedClickable(
        onClick = {},
        onLongClick = { isExpanded = !isExpanded }
      ),
    color = MaterialTheme.colorScheme.surfaceContainer
  ) {
    Box(
      modifier = Modifier
        .fillMaxSize()
        .padding(horizontal = 16.dp),
      contentAlignment = Alignment.CenterStart
    ) {
      if (!isExpanded) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          Box(
            modifier = Modifier
              .size(10.dp)
              .background(statusColor, CircleShape)
          )
          Spacer(Modifier.width(8.dp))
          Text(statusText, style = MaterialTheme.typography.labelMedium)
        }
      } else {
        Column(verticalArrangement = Arrangement.Center) {
          Text(
            stringResource(id = R.string.debug_connections_details_title),
            style = MaterialTheme.typography.titleSmall
          )
          Spacer(Modifier.height(4.dp))
          Text(
            stringResource(
              id = R.string.debug_connections_status,
              connectionState.getResourceString(context)
            ), style = MaterialTheme.typography.bodyMedium
          )
        }
      }
    }
  }
}
