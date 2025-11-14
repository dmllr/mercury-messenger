package hela.eillance.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.CallEnd
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.FloatingActionButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import hela.eillance.MainViewModel
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.webrtc.WebRtcTransport
import org.webrtc.SurfaceViewRenderer

@Composable
fun CallScreen(
  viewModel: MainViewModel,
  callState: CallState,
  onNavigateBack: () -> Unit
) {
  LaunchedEffect(callState.isInCall) {
    if (!callState.isInCall) {
      onNavigateBack()
    }
  }

  DisposableEffect(Unit) {
    onDispose {
      viewModel.stopCallMedia()
    }
  }

  val isVideoCall = callState.callType == HistoryEntryKind.VideoCall
  val eglBaseContext = remember { WebRtcTransport.eglBase.eglBaseContext }

  Box(modifier = Modifier
    .fillMaxSize()
    .background(Color.Black)) {

    if (isVideoCall) {
      AndroidView(
        factory = {
          SurfaceViewRenderer(it).apply {
            init(eglBaseContext, null)
          }
        },
        modifier = Modifier.fillMaxSize(),
        update = { surfaceViewRenderer ->
          viewModel.setRemoteVideoSink(callState.peerId, surfaceViewRenderer)
        },
        onRelease = { surfaceViewRenderer ->
          viewModel.setRemoteVideoSink(callState.peerId, null)
          surfaceViewRenderer.release()
        }
      )
    }

    Column(
      modifier = Modifier.fillMaxSize(),
      horizontalAlignment = Alignment.CenterHorizontally,
      verticalArrangement = Arrangement.SpaceBetween
    ) {
      Column(
        modifier = Modifier.padding(top = 48.dp),
        horizontalAlignment = Alignment.CenterHorizontally
      ) {
        Text(
          text = callState.peerId,
          style = MaterialTheme.typography.headlineSmall,
          color = Color.White,
          maxLines = 1,
          overflow = TextOverflow.Ellipsis
        )
        Spacer(modifier = Modifier.padding(4.dp))
        Text(
          text = "Calling...", // TODO: Add call timer
          style = MaterialTheme.typography.bodyLarge,
          color = Color.White.copy(alpha = 0.7f)
        )
      }

      if (isVideoCall) {
        Box(
          modifier = Modifier
            .fillMaxWidth()
            .padding(16.dp),
          contentAlignment = Alignment.TopEnd
        ) {
          AndroidView(
            factory = {
              SurfaceViewRenderer(it).apply {
                init(eglBaseContext, null)
                setMirror(true)
              }
            },
            modifier = Modifier
              .width(120.dp)
              .aspectRatio(0.75f)
              .clip(RoundedCornerShape(16.dp)),
            update = { surfaceViewRenderer ->
              viewModel.setLocalVideoSink(surfaceViewRenderer)
            },
            onRelease = { surfaceViewRenderer ->
              viewModel.setLocalVideoSink(null)
              surfaceViewRenderer.release()
            }
          )
        }
      }

      Row(
        modifier = Modifier
          .fillMaxWidth()
          .padding(bottom = 48.dp, start = 24.dp, end = 24.dp),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically
      ) {
        IconButton(onClick = { /* TODO */ }) {
          Icon(Icons.Default.Mic, contentDescription = "Mute", tint = Color.White, modifier = Modifier.size(32.dp))
        }

        FloatingActionButton(
          onClick = { viewModel.hangup() },
          containerColor = MaterialTheme.colorScheme.error,
          elevation = FloatingActionButtonDefaults.elevation(0.dp)
        ) {
          Icon(
            Icons.Default.CallEnd,
            contentDescription = "Hang Up",
            tint = Color.White,
            modifier = Modifier.size(36.dp)
          )
        }

        IconButton(onClick = { /* TODO */ }) {
          Icon(Icons.AutoMirrored.Filled.VolumeUp, contentDescription = "Speaker", tint = Color.White, modifier = Modifier.size(32.dp))
        }
      }
    }
  }
}
