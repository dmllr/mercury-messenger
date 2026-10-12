package hela.eillance.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
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

@Composable
fun CallUserInfo(
  name: String?,
  peerId: String,
  photo: String?,
  statusText: String,
  modifier: Modifier = Modifier,
  showPulse: Boolean = false,
  textColor: Color = MaterialTheme.colorScheme.onSurface,
) {
  Column(
    modifier = modifier,
    horizontalAlignment = Alignment.CenterHorizontally
  ) {
    Box(contentAlignment = Alignment.Center) {
      if (showPulse) {
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

        Box(
          modifier = Modifier
            .size(140.dp)
            .scale(pulseScale)
            .clip(CircleShape)
            .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.1f))
        )
      }

      UserAvatar(
        photoBase64 = photo,
        modifier = Modifier.size(120.dp),
        fallbackBackground = MaterialTheme.colorScheme.secondaryContainer,
        fallbackTint = MaterialTheme.colorScheme.onSecondaryContainer
      )
    }

    Spacer(modifier = Modifier.height(24.dp))

    Text(
      text = name ?: stringResource(id = R.string.unknown),
      style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
      color = textColor
    )

    Spacer(modifier = Modifier.height(8.dp))

    Text(
      text = statusText,
      style = MaterialTheme.typography.bodyLarge,
      color = textColor.copy(alpha = 0.8f)
    )

    if (name != null && !peerId.startsWith(name)) {
      Spacer(modifier = Modifier.height(4.dp))
      Text(
        text = peerId.take(8),
        fontSize = 14.sp,
        color = textColor.copy(alpha = 0.6f)
      )
    }
  }
}
