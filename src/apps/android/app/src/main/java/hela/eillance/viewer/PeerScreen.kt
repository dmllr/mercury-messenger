package hela.eillance.viewer

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.pullrefresh.PullRefreshIndicator
import androidx.compose.material.pullrefresh.pullRefresh
import androidx.compose.material.pullrefresh.rememberPullRefreshState
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.DefaultAlpha
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.unit.dp
import hela.eillance.theme.AppIcons
import hela.eillance.theme.extendedColors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterialApi::class)
@Composable
fun PeersScreen(
  peers: List<StoredPeer>,
  onPeerClicked: (StoredPeer) -> Unit,
  onRemovePeer: (String) -> Unit,
  onRefreshPeers: () -> Unit,
  isRefreshing: Boolean,
) {
  val peerStore = PeerStore(LocalContext.current, rememberCoroutineScope())

  val pullRefreshState = rememberPullRefreshState(
    refreshing = isRefreshing,
    onRefresh = onRefreshPeers
  )

  Box(
    modifier = Modifier
      .fillMaxSize()
      .pullRefresh(pullRefreshState)
  ) {
    Column(
      modifier = Modifier.fillMaxSize()
    ) {
      // Section heading - consistent with OperationScreen
      Text(
        text = "Known Devices",
        style = MaterialTheme.typography.titleMedium,
        modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 8.dp)
      )

      if (peers.isEmpty() && !isRefreshing) {
        Box(
          modifier = Modifier.fillMaxSize(),
          contentAlignment = Alignment.Center
        ) {
          Text(
            text = "No known devices",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
          )
        }
      } else {
        LazyColumn(
          modifier = Modifier.fillMaxSize(),
          contentPadding = PaddingValues(vertical = 8.dp)
        ) {
          items(peers, key = { it.id }) { peer ->
            PeerItem(
              peer = peer,
              onPeerClicked = onPeerClicked,
              onRemoveClicked = { onRemovePeer(peer.id) },
              peerStore = peerStore
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

@Composable
fun PeerItem(
  peer: StoredPeer,
  onPeerClicked: (StoredPeer) -> Unit,
  onRemoveClicked: () -> Unit,
  peerStore: PeerStore
) {
  var bitmap by remember { mutableStateOf<android.graphics.Bitmap?>(null) }
  var showMenu by remember { mutableStateOf(false) }

  LaunchedEffect(peer.previewImagePath) {
    launch(Dispatchers.IO) {
      bitmap = peerStore.loadPreviewBitmap(peer.previewImagePath)
    }
  }

  Card(
    modifier = Modifier
      .fillMaxWidth()
      .height(180.dp)
      .padding(horizontal = 16.dp, vertical = 6.dp)
      .clickable { onPeerClicked(peer) },
    elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
  ) {
    Box(
      modifier = Modifier.fillMaxSize()
    ) {
      // Preview Image as Background
      Box(
        modifier = Modifier
          .fillMaxSize()
          .background(Color.Gray.copy(alpha = 0.3f)),
        contentAlignment = Alignment.Center
      ) {
        if (bitmap != null) {
          Image(
            painter = BitmapPainter(bitmap!!.asImageBitmap()),
            contentDescription = "${peer.name} preview",
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Crop,
            colorFilter = if (!peer.isOnline) {
              ColorFilter.colorMatrix(androidx.compose.ui.graphics.ColorMatrix().apply { setToSaturation(0f) })
            } else null,
            alpha = if (!peer.isOnline) 0.6f else DefaultAlpha
          )
        } else {
          Icon(
            AppIcons.Failure,
            contentDescription = "No preview",
            tint = Color.Gray,
            modifier = Modifier.size(48.dp)
          )
        }
      }

      // Semi-transparent bar at the bottom with peer info and controls
      Box(
        modifier = Modifier
          .align(Alignment.BottomCenter)
          .fillMaxWidth()
          .background(Color.Black.copy(alpha = 0.5f))
          .padding(12.dp)
      ) {
        Row(
          modifier = Modifier.fillMaxWidth(),
          verticalAlignment = Alignment.CenterVertically
        ) {
          // Peer Name and Status
          Column(modifier = Modifier.weight(1f)) {
            Text(
              text = peer.name,
              style = MaterialTheme.typography.titleMedium,
              color = Color.White
            )
            Spacer(Modifier.height(4.dp))
            Row(
              verticalAlignment = Alignment.CenterVertically
            ) {
              // Online/Offline Indicator Icon
              val tint = if (peer.isOnline)
                MaterialTheme.extendedColors.success else MaterialTheme.extendedColors.offline
              Icon(
                imageVector = if (peer.isOnline) AppIcons.Status1Standby else AppIcons.Failure,
                contentDescription = if (peer.isOnline) "Online" else "Offline",
                tint = tint,
                modifier = Modifier.size(16.dp)
              )
              Spacer(Modifier.width(4.dp))
              Text(
                text = if (peer.isOnline) "Online" else "Offline",
                style = MaterialTheme.typography.bodyMedium,
                color = tint,
                fontStyle = if (!peer.isOnline) FontStyle.Italic else FontStyle.Normal
              )
            }
          }

          // Three dots menu
          Box {
            IconButton(
              onClick = { showMenu = true },
              modifier = Modifier.size(32.dp)
            ) {
              Icon(
                imageVector = Icons.Filled.MoreVert,
                contentDescription = "More options",
                tint = Color.White
              )
            }
            DropdownMenu(
              expanded = showMenu,
              onDismissRequest = { showMenu = false }
            ) {
              if (!peer.isOnline) {
                DropdownMenuItem(
                  text = { Text("Remove") },
                  onClick = {
                    onRemoveClicked()
                    showMenu = false
                  }
                )
              } else {
                DropdownMenuItem(
                  text = {
                    Text(
                      "Cannot remove online devices",
                      color = Color.Gray
                    )
                  },
                  onClick = { showMenu = false },
                  enabled = false
                )
              }
            }
          }
        }
      }
    }
  }
}
