package hela.eillance.components

import android.graphics.BitmapFactory
import android.util.Base64
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.PlayCircle
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material3.CircularProgressIndicator
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.request.ImageRequest
import coil.request.videoFrameMillis
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentStatus
import hela.eillance.ndkio.AttachmentType
import java.io.File
import java.util.Locale

@Composable
fun MediaBubbleContent(
  attachments: List<Attachment>,
  transferProgress: Map<String, Float>,
  isFromMe: Boolean,
  onMediaClick: (Attachment) -> Unit,
  onCancel: (String) -> Unit,
  onResume: (String) -> Unit
) {
  // Filter out canceled attachments for receiver to make them "disappear"
  val visibleAttachments = if (!isFromMe) {
    attachments.filter { it.status != AttachmentStatus.Canceled }
  } else {
    attachments
  }

  if (visibleAttachments.isEmpty()) return

  val roundedShape = RoundedCornerShape(8.dp)

  Column(
    modifier = Modifier
      .fillMaxWidth()
      .clip(roundedShape)
  ) {
    visibleAttachments.forEachIndexed { index, attachment ->
      if (index > 0) Spacer(modifier = Modifier.height(2.dp))
      MediaItem(
        attachment = attachment,
        progress = transferProgress[attachment.id],
        onClick = { onMediaClick(attachment) },
        onCancel = { onCancel(attachment.id) },
        onResume = { onResume(attachment.id) },
        modifier = Modifier.fillMaxWidth().aspectRatio(1.5f)
      )
    }
  }
}

@Composable
private fun MediaItem(
  attachment: Attachment,
  progress: Float?,
  onClick: () -> Unit,
  onCancel: () -> Unit,
  onResume: () -> Unit,
  modifier: Modifier = Modifier
) {
  Box(
    modifier = modifier
      .background(Color.Black.copy(alpha = 0.1f))
      .clickable(
        enabled = attachment.status == AttachmentStatus.Done,
        onClick = onClick
      ),
    contentAlignment = Alignment.Center
  ) {
    val file = File(attachment.path)
    val context = LocalContext.current

    val isVideo = attachment.type == AttachmentType.Video ||
        attachment.mime.startsWith("video/") ||
        file.extension.lowercase(Locale.US) in setOf("mp4", "mkv", "webm", "avi", "3gp", "mov")

    // Content Render
    if (attachment.status == AttachmentStatus.Done && file.exists()) {
      val model = remember(attachment.path, isVideo) {
        if (isVideo) {
          ImageRequest.Builder(context)
            .data(file)
            .decoderFactory { result, options, _ -> VideoFrameDecoder(result.source, options) }
            .videoFrameMillis(1000)
            .build()
        } else {
          file
        }
      }

      AsyncImage(
        model = model,
        contentDescription = "Attachment",
        modifier = Modifier.fillMaxSize(),
        contentScale = ContentScale.Crop
      )

      if (isVideo) {
        Icon(
          imageVector = Icons.Default.PlayCircle,
          contentDescription = "Play Video",
          tint = Color.White.copy(alpha = 0.8f),
          modifier = Modifier.size(48.dp)
        )
      }
    } else {
      // Thumbnail / Placeholder
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
          contentDescription = "Thumbnail",
          modifier = Modifier.fillMaxSize(),
          contentScale = ContentScale.Crop
        )
      } else {
        Box(
          modifier = Modifier.fillMaxSize().background(Color.Gray),
          contentAlignment = Alignment.Center
        ) {
          // Empty placeholder
        }
      }

      // Overlay
      Box(modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.4f)))
    }

    // Status Overlays
    Box(contentAlignment = Alignment.Center) {
      if (progress != null && progress >= 0f) {
        // Active Transfer
        Box(contentAlignment = Alignment.Center) {
          CircularProgressIndicator(
            progress = { progress },
            color = Color.White,
            modifier = Modifier.size(48.dp),
            trackColor = Color.White.copy(alpha = 0.4f),
            strokeWidth = 4.dp
          )
          IconButton(
            onClick = onCancel,
            modifier = Modifier
              .size(28.dp)
              .background(Color.Black.copy(alpha = 0.4f), CircleShape)
          ) {
            Icon(Icons.Default.Close, contentDescription = "Cancel", tint = Color.White, modifier = Modifier.size(20.dp))
          }
        }
      } else {
        when (attachment.status) {
          AttachmentStatus.Transferring -> {
            // Indeterminate state (initializing or stalled but active)
            Box(contentAlignment = Alignment.Center) {
              CircularProgressIndicator(
                color = Color.White,
                modifier = Modifier.size(48.dp),
                strokeWidth = 4.dp
              )
              IconButton(
                onClick = onCancel,
                modifier = Modifier
                  .size(28.dp)
                  .background(Color.Black.copy(alpha = 0.4f), CircleShape)
              ) {
                Icon(Icons.Default.Close, contentDescription = "Cancel", tint = Color.White, modifier = Modifier.size(20.dp))
              }
            }
          }
          AttachmentStatus.Pending -> {
            // Waiting / Interrupted state
            Row(
              modifier = Modifier
                .background(Color.Black.copy(alpha = 0.6f), CircleShape)
                .padding(horizontal = 12.dp, vertical = 8.dp)
                .clickable(onClick = onCancel),
              verticalAlignment = Alignment.CenterVertically,
              horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
              Icon(Icons.Default.Schedule, contentDescription = "Waiting", tint = Color.White, modifier = Modifier.size(20.dp))
              Icon(Icons.Default.Close, contentDescription = "Cancel", tint = Color.White, modifier = Modifier.size(20.dp))
            }
          }
          AttachmentStatus.Failed -> {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
              Icon(Icons.Default.Refresh, contentDescription = "Retry", tint = Color.Red, modifier = Modifier.size(32.dp).clickable { onResume() })
              Text("Failed", style = MaterialTheme.typography.labelSmall, color = Color.Red)
            }
          }
          AttachmentStatus.Canceled -> {
            // Only visible for Sender (receiver filters out canceled items)
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
              Icon(Icons.Default.PlayArrow, contentDescription = "Resume", tint = Color.White, modifier = Modifier.size(32.dp).clickable { onResume() })
              Text("Canceled", style = MaterialTheme.typography.labelSmall, color = Color.White)
            }
          }
          AttachmentStatus.Done -> {
            // Handled above (Media display)
          }
        }
      }
    }
  }
}
