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
import androidx.compose.material3.CircularProgressIndicator
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.components.VideoRenderer
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.webrtc.WebRtcTransport

@Composable
fun CallScreen(
  callVM: CallViewModel,
  callState: CallState,
  onNavigateBack: () -> Unit
) {
  LaunchedEffect(callState) {
    if (!callState.isInCall && !callState.isConnecting && !callState.isRinging) {
      onNavigateBack()
    }
  }

  DisposableEffect(Unit) {
    onDispose {
      callVM.stopCallMedia()
    }
  }

  val isVideoCall = callState.callType == HistoryEntryKind.VideoCall
  val eglBaseContext = remember { WebRtcTransport.eglBase.eglBaseContext }

  Box(modifier = Modifier
    .fillMaxSize()
    .background(Color.Black)) {

    if (isVideoCall && callState.isInCall) {
      // Remote Video (Full Screen)
      VideoRenderer(
        eglBaseContext = eglBaseContext,
        modifier = Modifier.fillMaxSize(),
        onUpdate = { sink -> callVM.setRemoteVideoSink(callState.peerId, sink) },
        onRelease = { callVM.setRemoteVideoSink(callState.peerId, null) }
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
        if (callState.isConnecting) {
          Spacer(modifier = Modifier.padding(4.dp))
          Text(text = stringResource(id = R.string.call_status_connecting), style = MaterialTheme.typography.bodyLarge, color = Color.White.copy(alpha = 0.7f))
        } else if (callState.isRinging && callState.callDirection == CallDirection.Outgoing) {
          Spacer(modifier = Modifier.padding(4.dp))
          Text(text = stringResource(id = R.string.call_status_ringing), style = MaterialTheme.typography.bodyLarge, color = Color.White.copy(alpha = 0.7f))
        } else if (callState.isInCall) {
          Spacer(modifier = Modifier.padding(4.dp))
          Text(text = "00:00", style = MaterialTheme.typography.bodyLarge, color = Color.White.copy(alpha = 0.7f))
        }
      }

      if (callState.isConnecting || (callState.isRinging && callState.callDirection == CallDirection.Outgoing)) {
        CircularProgressIndicator(modifier = Modifier.size(64.dp))
      } else if (isVideoCall && callState.isInCall) {
        Box(
          modifier = Modifier
            .fillMaxWidth()
            .padding(16.dp),
          contentAlignment = Alignment.TopEnd
        ) {
          // Local Video (PIP)
          VideoRenderer(
            eglBaseContext = eglBaseContext,
            modifier = Modifier
              .width(120.dp)
              .aspectRatio(0.75f)
              .clip(RoundedCornerShape(16.dp)),
            isMirror = true,
            zIndex = true,
            onUpdate = { sink -> callVM.setLocalVideoSink(sink) },
            onRelease = { callVM.setLocalVideoSink(null) }
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
          Icon(Icons.Default.Mic, contentDescription = stringResource(id = R.string.action_mute), tint = Color.White, modifier = Modifier.size(32.dp))
        }

        FloatingActionButton(
          onClick = { callVM.hangup() },
          containerColor = MaterialTheme.colorScheme.error,
          elevation = FloatingActionButtonDefaults.elevation(0.dp)
        ) {
          Icon(
            Icons.Default.CallEnd,
            contentDescription = stringResource(id = R.string.action_hangup),
            tint = Color.White,
            modifier = Modifier.size(36.dp)
          )
        }

        IconButton(onClick = { /* TODO */ }) {
          Icon(Icons.AutoMirrored.Filled.VolumeUp, contentDescription = stringResource(id = R.string.action_speaker), tint = Color.White, modifier = Modifier.size(32.dp))
        }
      }
    }
  }
}
