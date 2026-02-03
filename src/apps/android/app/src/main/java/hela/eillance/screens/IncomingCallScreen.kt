package hela.eillance.screens

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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import hela.eillance.camera.R
import hela.eillance.components.UserAvatar
import hela.eillance.theme.extendedColors

@Composable
fun IncomingCallScreen(
  callerId: String,
  callerName: String?,
  callerPhoto: String?,
  onAccept: () -> Unit,
  onReject: () -> Unit
) {
  Box(
    modifier = Modifier
      .fillMaxSize()
      .background(MaterialTheme.colorScheme.background)
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
          color = MaterialTheme.colorScheme.onBackground
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
          text = stringResource(id = R.string.call_type_mercury_audio),
          style = MaterialTheme.typography.bodyLarge,
          color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f)
        )

        if (callerName != null && callerId != callerName) {
          Spacer(modifier = Modifier.height(4.dp))
          Text(
            text = callerId,
            fontSize = 14.sp,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.5f)
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
          Text(stringResource(id = R.string.action_decline), style = MaterialTheme.typography.labelLarge)
        }

        // Accept Button
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
          IconButton(
            onClick = onAccept,
            modifier = Modifier.size(72.dp),
            colors = IconButtonDefaults.iconButtonColors(containerColor = MaterialTheme.extendedColors.success)
          ) {
            Icon(
              Icons.Default.Call,
              contentDescription = stringResource(id = R.string.action_accept),
              tint = Color.White,
              modifier = Modifier.size(32.dp)
            )
          }
          Spacer(modifier = Modifier.height(8.dp))
          Text(stringResource(id = R.string.action_accept), style = MaterialTheme.typography.labelLarge)
        }
      }
    }
  }
}
