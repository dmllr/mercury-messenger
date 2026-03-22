package hela.eillance.screens.conversation

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import hela.eillance.camera.R
import hela.eillance.ndkio.DraftAttachment
import java.io.File

@Composable
fun DraftAttachmentItem(
  draft: DraftAttachment,
  onRemove: () -> Unit,
  onClick: () -> Unit
) {
  Box(
    modifier = Modifier
      .width(100.dp)
      .clip(RoundedCornerShape(8.dp))
      .background(MaterialTheme.colorScheme.surface)
      .clickable(onClick = onClick)
  ) {
    Column {
      Box(
        modifier = Modifier
          .height(80.dp)
          .fillMaxWidth()
      ) {
        if (draft.thumbnailBase64.isNotEmpty()) {
          val thumbBitmap = remember(draft.thumbnailBase64) {
            try {
              val bytes = android.util.Base64.decode(draft.thumbnailBase64, android.util.Base64.DEFAULT)
              android.graphics.BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
            } catch (e: Exception) { null }
          }
          if (thumbBitmap != null) {
            Image(
              bitmap = thumbBitmap,
              contentDescription = null,
              contentScale = ContentScale.Crop,
              modifier = Modifier.fillMaxSize()
            )
          } else {
            AsyncImage(model = File(draft.path), contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxSize())
          }
        } else {
          val icon = getDraftIcon(draft.mimeType)
          Box(
            modifier = Modifier
              .fillMaxSize()
              .background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.Center
          ) {
            Icon(
              imageVector = icon,
              contentDescription = null,
              tint = MaterialTheme.colorScheme.onSurfaceVariant,
              modifier = Modifier.size(32.dp)
            )
          }
        }

        IconButton(
          onClick = onRemove,
          modifier = Modifier
            .align(Alignment.TopEnd)
            .size(24.dp)
            .background(Color.Black.copy(alpha = 0.5f), CircleShape)
        ) {
          Icon(
            Icons.Default.Close,
            contentDescription = stringResource(R.string.attachment_remove),
            tint = Color.White,
            modifier = Modifier.size(16.dp)
          )
        }
      }

      Column(modifier = Modifier.padding(4.dp)) {
        Text(
          text = draft.name,
          style = MaterialTheme.typography.labelSmall,
          fontWeight = FontWeight.Bold,
          maxLines = 1,
          overflow = TextOverflow.Ellipsis
        )
        if (draft.note.isNotEmpty()) {
          Text(
            text = draft.note,
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            color = MaterialTheme.colorScheme.primary
          )
        } else {
          Text(
            text = stringResource(R.string.attachment_note_placeholder),
            style = MaterialTheme.typography.labelSmall,
            fontStyle = FontStyle.Italic,
            color = MaterialTheme.colorScheme.onSurfaceVariant
          )
        }
      }
    }
  }
}
