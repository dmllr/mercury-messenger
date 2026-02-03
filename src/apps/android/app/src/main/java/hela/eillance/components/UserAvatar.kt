package hela.eillance.components

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.utils.rememberImageBitmapFromBase64

@Composable
fun UserAvatar(
  photoBase64: String?,
  modifier: Modifier = Modifier,
  shape: Shape = CircleShape,
  fallbackTint: Color = MaterialTheme.colorScheme.onSecondaryContainer,
  fallbackBackground: Color = MaterialTheme.colorScheme.secondaryContainer,
  contentDescription: String? = stringResource(R.string.user_avatar_content_description),
  onClick: (() -> Unit)? = null
) {
  val imageBitmap = rememberImageBitmapFromBase64(photoBase64)
  val clipModifier = modifier.clip(shape)
  val finalModifier = if (onClick != null) {
    clipModifier.clickable(onClick = onClick)
  } else {
    clipModifier
  }

  if (imageBitmap != null) {
    Image(
      bitmap = imageBitmap,
      contentDescription = contentDescription,
      modifier = finalModifier,
      contentScale = ContentScale.Crop
    )
  } else {
    Box(
      modifier = finalModifier
        .background(fallbackBackground),
      contentAlignment = Alignment.Center
    ) {
      Icon(
        imageVector = Icons.Default.Person,
        contentDescription = null,
        tint = fallbackTint,
        modifier = Modifier
          .padding(8.dp)
          .matchParentSize()
      )
    }
  }
}
