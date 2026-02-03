package hela.eillance.screens.debug

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
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
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.DebugConnectionInfo
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.viewmodels.DebugViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConnectionDebugScreen(
  debugVM: DebugViewModel,
  onBack: () -> Unit
) {
  var infos by remember { mutableStateOf<List<DebugConnectionInfo>>(emptyList()) }
  val context = LocalContext.current

  fun refresh() {
    infos = debugVM.getConnectionInfo()
  }

  LaunchedEffect(Unit) {
    refresh()
  }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_connections)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(id = R.string.action_back))
          }
        },
        actions = {
          IconButton(onClick = { refresh() }) {
            Icon(Icons.Default.Refresh, contentDescription = stringResource(id = R.string.action_refresh))
          }
        }
      )
    }
  ) { padding ->
    LazyColumn(modifier = Modifier.padding(padding)) {
      if (infos.isEmpty()) {
        item {
          Text(stringResource(id = R.string.debug_connections_empty), modifier = Modifier.padding(16.dp))
        }
      }
      items(infos) { info ->
        val color = when (info.connectionState) {
          PeerConnectionState.Connected -> Color.Green
          PeerConnectionState.Connecting -> Color.Yellow
          PeerConnectionState.Failed -> Color.Red
          else -> Color.Gray
        }

        ListItem(
          headlineContent = {
            Row(verticalAlignment = Alignment.CenterVertically) {
              Box(modifier = Modifier
                .size(8.dp)
                .background(color, CircleShape))
              Spacer(Modifier.width(8.dp))
              Text(info.peerId.take(12))
            }
          },
          supportingContent = {
            Column {
              Text("State: ${info.connectionState.getResourceString(context)}")
              val lastActivity =
                if (info.lastActivitySecAgo < 0) stringResource(id = R.string.debug_connections_last_activity_never) else stringResource(
                  id = R.string.debug_connections_last_activity,
                  info.lastActivitySecAgo
                )
              Text(lastActivity)
              Text(
                stringResource(
                  id = R.string.debug_connections_data_channel,
                  if (info.isDataChannelOpen) stringResource(id = R.string.debug_connections_open) else stringResource(
                    id = R.string.debug_connections_closed
                  )
                )
              )
              Text(stringResource(id = R.string.debug_connections_media_conn, info.hasMediaConnection))
            }
          }
        )
        HorizontalDivider()
      }
    }
  }
}
