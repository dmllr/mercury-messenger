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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Block
import androidx.compose.material.icons.filled.Cancel
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.PlayCircle
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Repeat
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
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.request.ImageRequest
import coil.request.videoFrameMillis
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentStatus
import hela.eillance.ndkio.AttachmentType
import java.io.File

@Composable
fun MediaBubbleContent(
  attachments: List<Attachment>,
  transferProgress: Map<String, Float>,
  isFromMe: Boolean,
  onMediaClick: (Attachment) -> Unit,
  onCancel: (String) -> Unit,
  onResume: (String) -> Unit,
  onDiscard: (String) -> Unit
) {
  val visibleAttachments = attachments.filter {
    it.status != AttachmentStatus.Discarded
  }

  if (visibleAttachments.isEmpty()) return

  val count = visibleAttachments.size
  val roundedShape = RoundedCornerShape(8.dp)

  val renderItem: @Composable (Int, Modifier) -> Unit = { index, modifier ->
    val att = visibleAttachments[index]
    MediaItem(
      attachment = att,
      progress = transferProgress[att.id],
      isFromMe = isFromMe,
      onClick = { onMediaClick(att) },
      onCancel = { onCancel(att.id) },
      onResume = { onResume(att.id) },
      onDiscard = { onDiscard(att.id) },
      modifier = modifier
    )
  }

  Column(
    modifier = Modifier
      .fillMaxWidth()
      .clip(roundedShape)
  ) {
    when (count) {
      1 -> renderItem(0, Modifier.fillMaxWidth().aspectRatio(1.5f))
      2 -> {
        Row(modifier = Modifier.fillMaxWidth().height(160.dp)) {
          renderItem(0, Modifier.weight(1f).fillMaxSize())
          Spacer(modifier = Modifier.width(2.dp))
          renderItem(1, Modifier.weight(1f).fillMaxSize())
        }
      }
      3 -> {
        Column(modifier = Modifier.fillMaxWidth()) {
          renderItem(0, Modifier.fillMaxWidth().height(160.dp))
          Spacer(modifier = Modifier.height(2.dp))
          Row(modifier = Modifier.fillMaxWidth().height(100.dp)) {
            renderItem(1, Modifier.weight(1f).fillMaxSize())
            Spacer(modifier = Modifier.width(2.dp))
            renderItem(2, Modifier.weight(1f).fillMaxSize())
          }
        }
      }
      else -> {
        Column(modifier = Modifier.fillMaxWidth()) {
          Row(modifier = Modifier.fillMaxWidth().height(120.dp)) {
            renderItem(0, Modifier.weight(1f).fillMaxSize())
            Spacer(modifier = Modifier.width(2.dp))
            renderItem(1, Modifier.weight(1f).fillMaxSize())
          }
          Spacer(modifier = Modifier.height(2.dp))
          Row(modifier = Modifier.fillMaxWidth().height(120.dp)) {
            renderItem(2, Modifier.weight(1f).fillMaxSize())
            Spacer(modifier = Modifier.width(2.dp))
            Box(modifier = Modifier.weight(1f).fillMaxSize()) {
              renderItem(3, Modifier.fillMaxSize())
              if (count > 4) {
                Box(
                  modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.5f))
                    .clickable { onMediaClick(visibleAttachments[3]) },
                  contentAlignment = Alignment.Center
                ) {
                  Text(text = "+${count - 4}", style = MaterialTheme.typography.titleLarge, color = Color.White)
                }
              }
            }
          }
        }
      }
    }
  }
}

@Composable
private fun MediaItem(
  attachment: Attachment,
  progress: Float?,
  isFromMe: Boolean,
  onClick: () -> Unit,
  onCancel: () -> Unit,
  onResume: () -> Unit,
  onDiscard: () -> Unit,
  modifier: Modifier = Modifier
) {
  // Sender Canceled -> No Thumbnail, just text/icon.
  if (attachment.status == AttachmentStatus.Canceled) {
    Box(
      modifier = modifier
        .background(MaterialTheme.colorScheme.surfaceContainerHigh)
        .padding(16.dp),
      contentAlignment = Alignment.Center
    ) {
      Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Icon(
          imageVector = Icons.Default.Block,
          contentDescription = "Canceled",
          tint = MaterialTheme.colorScheme.onSurfaceVariant,
          modifier = Modifier.size(24.dp)
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
          text = "This upload was canceled by the sender",
          style = MaterialTheme.typography.bodySmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
          textAlign = TextAlign.Center
        )
      }
    }
    return
  }

  // Thumbnail or placeholder
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
    val isVideo = attachment.type == AttachmentType.Video || attachment.mime.startsWith("video/")

    // Content Layer
    if (attachment.status == AttachmentStatus.Done && file.exists()) {
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
        contentDescription = "Attachment",
        modifier = Modifier.fillMaxSize(),
        contentScale = ContentScale.Crop
      )
      if (isVideo) {
        Icon(
          imageVector = Icons.Default.PlayCircle,
          contentDescription = "Play",
          tint = Color.White.copy(alpha = 0.8f),
          modifier = Modifier.size(48.dp)
        )
      }
    } else {
      // Thumbnail
      val imageBitmap = remember(attachment.thumbnail) {
        if (!attachment.thumbnail.isNullOrEmpty()) {
          try {
            val bytes = Base64.decode(attachment.thumbnail, Base64.DEFAULT)
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
          } catch (e: Exception) { null }
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
        Box(modifier = Modifier.fillMaxSize().background(Color.Gray))
      }
      // Darken overlay for non-done states
      Box(modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.4f)))
    }

    // Interactive Layer
    Box(contentAlignment = Alignment.Center) {
      if (attachment.status == AttachmentStatus.Paused) {
        // Scenario 2: Receiver Paused (Resumable)
        Row(
          horizontalArrangement = Arrangement.spacedBy(24.dp),
          verticalAlignment = Alignment.CenterVertically
        ) {
          // Resume
          Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(
              imageVector = Icons.Default.Repeat,
              contentDescription = "Resume",
              tint = Color.White,
              modifier = Modifier
                .size(36.dp)
                .background(Color.Black.copy(alpha = 0.3f), CircleShape)
                .clickable { onResume() }
                .padding(6.dp)
            )
            Text("Resume", style = MaterialTheme.typography.labelSmall, color = Color.White)
          }
          // Cancel Completely (Discard)
          Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(
              imageVector = Icons.Default.Cancel,
              contentDescription = "Cancel",
              tint = Color.White,
              modifier = Modifier
                .size(36.dp)
                .background(Color.Black.copy(alpha = 0.3f), CircleShape)
                .clickable { onDiscard() }
                .padding(6.dp)
            )
            Text("Cancel", style = MaterialTheme.typography.labelSmall, color = Color.White)
          }
        }
      } else if (progress != null && progress >= 0f) {
        // Active Transfer
        Box(contentAlignment = Alignment.Center) {
          CircularProgressIndicator(
            progress = progress,
            modifier = Modifier.size(48.dp),
            color = Color.White,
            strokeWidth = 4.dp,
            trackColor = Color.White.copy(alpha = 0.4f),
          )
          IconButton(
            onClick = onCancel, // Sets status to Paused (download) or Canceled (upload)
            modifier = Modifier
              .size(28.dp)
              .background(Color.Black.copy(alpha = 0.4f), CircleShape)
          ) {
            Icon(Icons.Default.Close, contentDescription = "Pause", tint = Color.White, modifier = Modifier.size(20.dp))
          }
        }
      } else {
        when (attachment.status) {
          AttachmentStatus.Transferring -> {
            // Indeterminate / Initializing
            Box(contentAlignment = Alignment.Center) {
              CircularProgressIndicator(color = Color.White, modifier = Modifier.size(48.dp), strokeWidth = 4.dp)
              IconButton(
                onClick = onCancel,
                modifier = Modifier.size(28.dp).background(Color.Black.copy(alpha = 0.4f), CircleShape)
              ) {
                Icon(Icons.Default.Close, contentDescription = "Cancel", tint = Color.White, modifier = Modifier.size(20.dp))
              }
            }
          }
          AttachmentStatus.Pending -> {
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
          else -> {}
        }
      }
    }
  }
}
