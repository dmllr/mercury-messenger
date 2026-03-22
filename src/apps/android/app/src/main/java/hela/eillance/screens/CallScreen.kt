package hela.eillance.screens

import androidx.activity.compose.BackHandler
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
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.FloatingActionButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.components.CallUserInfo
import hela.eillance.components.VideoRenderer
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.theme.extendedColors
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.webrtc.WebRtcTransport
import kotlinx.coroutines.delay
import java.util.concurrent.TimeUnit

@Composable
fun CallScreen(
  callVM: CallViewModel,
  callState: CallState,
  contactName: String?,
  contactPhoto: String?
) {
  // Prevent back navigation to ensure call screen stays prioritized
  BackHandler { }

  DisposableEffect(Unit) {
    onDispose {
      callVM.stopCallMedia()
    }
  }

  var callDurationSeconds by remember { mutableLongStateOf(0L) }

  LaunchedEffect(callState.isInCall) {
    if (callState.isInCall) {
      val startTime = System.currentTimeMillis()
      while (true) {
        callDurationSeconds = (System.currentTimeMillis() - startTime) / 1000
        delay(1000)
      }
    }
  }

  val isVideoCall = callState.callType == HistoryEntryKind.VideoCall
  val eglBaseContext = remember { WebRtcTransport.eglBase.eglBaseContext }

  // Determine if we show the video UI or the standard "profile" UI
  val showVideoUI = isVideoCall && callState.isInCall

  // Status Text Logic
  val statusText = when {
    callState.isConnecting -> "Connecting..." // Matches requirement
    callState.isRinging && callState.callDirection == CallDirection.Outgoing -> stringResource(id = R.string.call_status_ringing)
    callState.isInCall -> {
      val minutes = TimeUnit.SECONDS.toMinutes(callDurationSeconds)
      val seconds = callDurationSeconds - TimeUnit.MINUTES.toSeconds(minutes)
      String.format("%02d:%02d", minutes, seconds)
    }

    else -> ""
  }

  Box(
    modifier = Modifier
      .fillMaxSize()
      .background(Color.Black)
  ) {

    // --- VIDEO LAYER (Only if Active Video) ---
    if (showVideoUI) {
      // Remote Video (Full Screen)
      VideoRenderer(
        eglBaseContext = eglBaseContext,
        modifier = Modifier.fillMaxSize(),
        onUpdate = { sink -> callVM.setRemoteVideoSink(callState.peerId, sink) },
        onRelease = { callVM.setRemoteVideoSink(callState.peerId, null) }
      )
    }

    // --- INFO & CONTROLS LAYER ---
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(bottom = 48.dp),
      horizontalAlignment = Alignment.CenterHorizontally,
      verticalArrangement = Arrangement.SpaceBetween
    ) {

      // Top Section: Info
      if (!showVideoUI) {
        CallUserInfo(
          name = contactName,
          peerId = callState.peerId,
          photo = contactPhoto,
          statusText = statusText,
          showPulse = callState.isConnecting || (callState.isRinging && callState.callDirection == CallDirection.Outgoing),
          modifier = Modifier.padding(top = 100.dp)
        )
      } else {
        Column(
          modifier = Modifier.padding(top = 48.dp),
          horizontalAlignment = Alignment.CenterHorizontally
        ) {
          // Replicating CallUserInfo text style for consistency
          androidx.compose.material3.Text(
            text = contactName ?: stringResource(id = R.string.unknown),
            style = MaterialTheme.typography.headlineSmall, // Similar to CallUserInfo but Small to save space
            color = Color.White,
            maxLines = 1,
            overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis
          )
          androidx.compose.material3.Text(
            text = statusText,
            style = MaterialTheme.typography.bodyMedium,
            color = Color.White.copy(alpha = 0.8f)
          )
        }
      }

      // Center: PIP (Only for Video)
      if (showVideoUI) {
        Box(
          modifier = Modifier
            .fillMaxWidth()
            .weight(1f) // Take up remaining space to push controls down
            .padding(16.dp),
          contentAlignment = Alignment.TopEnd
        ) {
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
      } else {
        // Spacer for Audio mode to push controls to bottom
        Spacer(modifier = Modifier.weight(1f))
      }

      // Bottom: Controls
      Row(
        modifier = Modifier
          .fillMaxWidth()
          .padding(horizontal = 48.dp), // Align with IncomingScreen horizontal padding
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically
      ) {
        IconButton(onClick = { callVM.toggleMute() }) {
          Icon(
            if (callState.isMuted) Icons.Default.MicOff else Icons.Default.Mic,
            contentDescription = stringResource(id = R.string.action_mute),
            tint = if (callState.isMuted) MaterialTheme.extendedColors.warning else Color.White,
            modifier = Modifier.size(32.dp)
          )
        }

        FloatingActionButton(
          onClick = { callVM.hangup() },
          containerColor = MaterialTheme.colorScheme.error,
          elevation = FloatingActionButtonDefaults.elevation(0.dp),
          modifier = Modifier.size(72.dp) // Match Incoming Reject button size
        ) {
          Icon(
            Icons.Default.CallEnd,
            contentDescription = stringResource(id = R.string.action_hangup),
            tint = Color.White,
            modifier = Modifier.size(32.dp)
          )
        }

        IconButton(onClick = { callVM.toggleSpeaker() }) {
          Icon(
            Icons.AutoMirrored.Filled.VolumeUp,
            contentDescription = stringResource(id = R.string.action_speaker),
            tint = if (callState.isSpeakerOn) MaterialTheme.extendedColors.warning else Color.White,
            modifier = Modifier.size(32.dp)
          )
        }
      }
    }
  }
}
