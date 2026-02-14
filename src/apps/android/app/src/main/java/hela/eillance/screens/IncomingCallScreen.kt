package hela.eillance.screens

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.BackHandler
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.CallEnd
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import hela.eillance.camera.R
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.theme.extendedColors
import hela.eillance.viewmodels.CallViewModel

@Composable
fun IncomingCallScreen(
  callerId: String,
  callerName: String?,
  callerPhoto: String?,
  callType: HistoryEntryKind,
  callVM: CallViewModel,
  onAccept: () -> Unit,
  onReject: () -> Unit
) {
  // Disable back navigation to ensure the user explicitly Accepts or Rejects
  BackHandler { }

  val context = LocalContext.current
  val isVideoCall = callType == HistoryEntryKind.VideoCall

  val micGranted by remember {
    mutableStateOf(
      ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
    )
  }
  val cameraGranted by remember {
    mutableStateOf(
      ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED
    )
  }

  // Can accept if we have at least Microphone permission (since audio is minimum req)
  val canAccept = micGranted

  // Warning if it's a video call but we lack camera (will fallback to audio-only on our side)
  val showCameraWarning = isVideoCall && !cameraGranted

  Box(
    modifier = Modifier
      .fillMaxSize()
      .background(Color.Black)
  ) {
    // Background pulsing effect for the avatar
    val infiniteTransition = rememberInfiniteTransition(label = "pulse")
    val pulseScale by infiniteTransition.animateFloat(
      initialValue = 1f,
      targetValue = 1.2f,
      animationSpec = infiniteRepeatable(
        animation = tween(1500, easing = LinearEasing),
        repeatMode = RepeatMode.Reverse
      ),
      label = "pulseScale"
    )

    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(top = 100.dp, bottom = 64.dp),
      horizontalAlignment = Alignment.CenterHorizontally,
      verticalArrangement = Arrangement.SpaceBetween
    ) {
      // Caller Info
      Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(contentAlignment = Alignment.Center) {
          // Pulse Circle
          Box(
            modifier = Modifier
              .size(140.dp)
              .scale(pulseScale)
              .clip(CircleShape)
              .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.1f))
          )

          // Avatar
          UserAvatar(
            photoBase64 = callerPhoto,
            modifier = Modifier.size(120.dp),
            fallbackBackground = MaterialTheme.colorScheme.secondaryContainer,
            fallbackTint = MaterialTheme.colorScheme.onSecondaryContainer
          )
        }

        Spacer(modifier = Modifier.height(24.dp))

        Text(
          text = callerName ?: stringResource(id = R.string.unknown),
          style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
          color = Color.White
        )

        Spacer(modifier = Modifier.height(8.dp))

        val callTypeRes = if (isVideoCall) R.string.call_incoming_video else R.string.call_incoming_voice
        Text(
          text = stringResource(id = callTypeRes),
          style = MaterialTheme.typography.bodyLarge,
          color = Color.White.copy(alpha = 0.8f)
        )

        if (callerName != null && callerId != callerName) {
          Spacer(modifier = Modifier.height(4.dp))
          Text(
            text = callerId.take(8),
            fontSize = 14.sp,
            color = Color.White.copy(alpha = 0.6f)
          )
        }
      }

      // Permission Warnings
      if (!micGranted) {
        Box(
          modifier = Modifier
            .padding(horizontal = 32.dp)
            .background(MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.8f), MaterialTheme.shapes.medium)
            .padding(16.dp)
        ) {
          Text(
            text = stringResource(id = R.string.incoming_call_mic_missing),
            color = MaterialTheme.colorScheme.onErrorContainer,
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center
          )
        }
      } else if (showCameraWarning) {
        Box(
          modifier = Modifier
            .padding(horizontal = 32.dp)
            .background(MaterialTheme.extendedColors.warning.copy(alpha = 0.8f), MaterialTheme.shapes.medium)
            .padding(16.dp)
        ) {
          Text(
            text = stringResource(id = R.string.incoming_call_cam_missing),
            color = Color.Black,
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center
          )
        }
      }

      // Actions
      Row(
        modifier = Modifier
          .fillMaxWidth()
          .padding(horizontal = 48.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
      ) {
        // Reject Button
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
          IconButton(
            onClick = onReject,
            modifier = Modifier.size(72.dp),
            colors = IconButtonDefaults.iconButtonColors(containerColor = MaterialTheme.colorScheme.error)
          ) {
            Icon(
              Icons.Default.CallEnd,
              contentDescription = stringResource(id = R.string.action_reject),
              tint = Color.White,
              modifier = Modifier.size(32.dp)
            )
          }
          Spacer(modifier = Modifier.height(8.dp))
          Text(
            stringResource(id = R.string.action_decline),
            style = MaterialTheme.typography.labelLarge,
            color = Color.White
          )
        }

        // Accept Button
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
          val containerColor = if (canAccept) MaterialTheme.extendedColors.success else Color.Gray.copy(alpha = 0.5f)
          IconButton(
            onClick = { if (canAccept) onAccept() },
            modifier = Modifier.size(72.dp),
            enabled = canAccept,
            colors = IconButtonDefaults.iconButtonColors(
              containerColor = containerColor,
              disabledContainerColor = Color.Gray.copy(alpha = 0.3f)
            )
          ) {
            Icon(
              Icons.Default.Call,
              contentDescription = stringResource(id = R.string.action_accept),
              tint = if (canAccept) Color.White else Color.White.copy(alpha = 0.5f),
              modifier = Modifier.size(32.dp)
            )
          }
          Spacer(modifier = Modifier.height(8.dp))
          Text(
            stringResource(id = R.string.action_accept),
            style = MaterialTheme.typography.labelLarge,
            color = if (canAccept) Color.White else Color.White.copy(alpha = 0.5f)
          )
        }
      }
    }
  }
}
