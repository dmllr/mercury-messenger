package hela.eillance.components

import android.graphics.BitmapFactory
import android.util.Base64
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BrokenImage
import androidx.compose.material.icons.filled.PlayCircle
import androidx.compose.material.icons.filled.VideoFile
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.request.ImageRequest
import coil.request.videoFrameMillis
import hela.eillance.camera.R
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentStatus
import hela.eillance.ndkio.AttachmentType
import java.io.File

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun MediaItem(
  attachment: Attachment,
  progress: Float?,
  isFromMe: Boolean,
  selectionMode: Boolean,
  onClick: () -> Unit,
  onLongClick: (() -> Unit)? = null,
  onCancel: () -> Unit,
  onResume: () -> Unit,
  onDiscard: () -> Unit,
  modifier: Modifier = Modifier
) {
  val isDone = attachment.status == AttachmentStatus.Done
  val isVideo = attachment.type == AttachmentType.Video || attachment.mime.startsWith("video/")
  val file = File(attachment.path)
  val fileExists = file.exists()
  val isGif = attachment.mime == "image/gif" || file.extension.lowercase() == "gif"
  val context = LocalContext.current

  Box(
    modifier = modifier
      .background(MaterialTheme.colorScheme.surfaceContainerHigh)
      .combinedClickable(
        enabled = isDone || selectionMode,
        onClick = { if (selectionMode) onLongClick?.invoke() else onClick() },
        onLongClick = { onLongClick?.invoke() }
      ),
    contentAlignment = Alignment.Center
  ) {
    val file = File(attachment.path)
    val context = LocalContext.current

    // --- BASE LAYER (Thumbnail/Image/Video) ---
    if (isDone && file.exists()) {
      val model = remember(attachment.path, isVideo) {
        if (isVideo) {
          ImageRequest.Builder(context)
            .data(file)
            .decoderFactory { result, options, _ -> VideoFrameDecoder(result.source, options) }
            .videoFrameMillis(1000)
            .build()
        } else file
      }
      AsyncImage(
        model = model,
        contentDescription = stringResource(id = R.string.attachment_content_description),
        modifier = Modifier.fillMaxSize(),
        contentScale = ContentScale.Crop
      )
    } else {
      // Show thumbnail if available, otherwise placeholder
      val imageBitmap = remember(attachment.thumbnail) {
        if (!attachment.thumbnail.isNullOrEmpty()) {
          try {
            val bytes = Base64.decode(attachment.thumbnail, Base64.DEFAULT)
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
          } catch (e: Exception) {
            null
          }
        } else null
      }

      if (imageBitmap != null) {
        Image(
          bitmap = imageBitmap,
          contentDescription = stringResource(id = R.string.thumbnail_content_description),
          modifier = Modifier.fillMaxSize(),
          contentScale = ContentScale.Crop
        )
      } else {
        Box(
          modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.surfaceVariant),
          contentAlignment = Alignment.Center
        ) {
          Icon(
            imageVector = if (isVideo) Icons.Default.VideoFile else Icons.Default.BrokenImage,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(32.dp)
          )
        }
      }
    }

    // --- OVERLAY LAYER (Darkens the image for status states) ---
    if (!isDone) {
      Box(
        modifier = Modifier
          .fillMaxSize()
          .background(Color.Black.copy(alpha = 0.3f))
      )
    }

    // --- CENTER PLAY ICON (Only for Video & Done) ---
    if (isVideo && isDone) {
      Icon(
        imageVector = Icons.Default.PlayCircle,
        contentDescription = stringResource(id = R.string.action_play),
        tint = Color.White.copy(alpha = 0.8f),
        modifier = Modifier.size(48.dp)
      )
    }

    // --- BOTTOM OVERLAY (File Info & Controls) ---
    // Always visible to show file type icon, name, status, etc.
    Box(
      modifier = Modifier
        .align(Alignment.BottomCenter)
        .fillMaxWidth()
        .background(
          Brush.verticalGradient(
            colors = listOf(Color.Transparent, Color.Black.copy(alpha = 0.8f))
          )
        )
        .padding(8.dp)
    ) {
      AttachmentInfoRow(
        attachment = attachment,
        progress = progress,
        isFromMe = isFromMe,
        onCancel = onCancel,
        onResume = onResume,
        onDiscard = onDiscard,
        textColor = Color.White,
        subTextColor = Color.White.copy(alpha = 0.8f),
        iconTint = Color.White,
        iconBackground = Color.White.copy(alpha = 0.2f)
      )
    }
  }
}
