package hela.eillance.components

import android.graphics.BitmapFactory
import android.util.Base64
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.PlayCircle
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
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
import hela.eillance.ndkio.AttachmentType
import java.io.File
import java.util.Locale

@Composable
fun MediaBubbleContent(
  attachments: List<Attachment>,
  transferProgress: Map<String, Float>,
  onMediaClick: (Attachment) -> Unit
) {
  if (attachments.isEmpty()) return

  val count = attachments.size
  val roundedShape = RoundedCornerShape(8.dp)

  Column(
    modifier = Modifier
      .fillMaxWidth()
      .clip(roundedShape)
  ) {
    when (count) {
      1 -> {
        // Single Image: Full width, dynamic height (but usually constrained by bubble)
        // We assume a standard aspect ratio for the bubble container or let it fill.
        MediaItem(
          attachment = attachments[0],
          progress = transferProgress[attachments[0].id],
          onClick = { onMediaClick(attachments[0]) },
          modifier = Modifier.fillMaxWidth().aspectRatio(1.5f) // 3:2 landscape default
        )
      }
      2 -> {
        // Two Images: Side by side
        Row(modifier = Modifier.fillMaxWidth().height(160.dp)) {
          MediaItem(
            attachment = attachments[0],
            progress = transferProgress[attachments[0].id],
            onClick = { onMediaClick(attachments[0]) },
            modifier = Modifier.weight(1f).fillMaxSize()
          )
          Spacer(modifier = Modifier.width(2.dp))
          MediaItem(
            attachment = attachments[1],
            progress = transferProgress[attachments[1].id],
            onClick = { onMediaClick(attachments[1]) },
            modifier = Modifier.weight(1f).fillMaxSize()
          )
        }
      }
      3 -> {
        // Three Images: One big on top, two small below
        Column(modifier = Modifier.fillMaxWidth()) {
          MediaItem(
            attachment = attachments[0],
            progress = transferProgress[attachments[0].id],
            onClick = { onMediaClick(attachments[0]) },
            modifier = Modifier.fillMaxWidth().height(160.dp)
          )
          Spacer(modifier = Modifier.height(2.dp))
          Row(modifier = Modifier.fillMaxWidth().height(100.dp)) {
            MediaItem(
              attachment = attachments[1],
              progress = transferProgress[attachments[1].id],
              onClick = { onMediaClick(attachments[1]) },
              modifier = Modifier.weight(1f).fillMaxSize()
            )
            Spacer(modifier = Modifier.width(2.dp))
            MediaItem(
              attachment = attachments[2],
              progress = transferProgress[attachments[2].id],
              onClick = { onMediaClick(attachments[2]) },
              modifier = Modifier.weight(1f).fillMaxSize()
            )
          }
        }
      }
      else -> {
        // 4 or more: 2x2 Grid. The 4th item shows overlay if count > 4.
        Column(modifier = Modifier.fillMaxWidth()) {
          Row(modifier = Modifier.fillMaxWidth().height(120.dp)) {
            MediaItem(
              attachment = attachments[0],
              progress = transferProgress[attachments[0].id],
              onClick = { onMediaClick(attachments[0]) },
              modifier = Modifier.weight(1f).fillMaxSize()
            )
            Spacer(modifier = Modifier.width(2.dp))
            MediaItem(
              attachment = attachments[1],
              progress = transferProgress[attachments[1].id],
              onClick = { onMediaClick(attachments[1]) },
              modifier = Modifier.weight(1f).fillMaxSize()
            )
          }
          Spacer(modifier = Modifier.height(2.dp))
          Row(modifier = Modifier.fillMaxWidth().height(120.dp)) {
            MediaItem(
              attachment = attachments[2],
              progress = transferProgress[attachments[2].id],
              onClick = { onMediaClick(attachments[2]) },
              modifier = Modifier.weight(1f).fillMaxSize()
            )
            Spacer(modifier = Modifier.width(2.dp))

            Box(modifier = Modifier.weight(1f).fillMaxSize()) {
              MediaItem(
                attachment = attachments[3],
                progress = transferProgress[attachments[3].id],
                onClick = { onMediaClick(attachments[3]) },
                modifier = Modifier.fillMaxSize()
              )
              // Overlay for remaining items
              if (count > 4) {
                Box(
                  modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.5f))
                    .clickable { onMediaClick(attachments[3]) }, // Clicking overlay opens carousel at index 3
                  contentAlignment = Alignment.Center
                ) {
                  Text(
                    text = "+${count - 4}",
                    style = MaterialTheme.typography.titleLarge,
                    color = Color.White
                  )
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
  onClick: () -> Unit,
  modifier: Modifier = Modifier
) {
  Box(
    modifier = modifier
      .background(Color.Black.copy(alpha = 0.1f))
      .clickable(enabled = attachment.isDownloaded, onClick = onClick),
    contentAlignment = Alignment.Center
  ) {
    val file = File(attachment.path)
    val context = LocalContext.current

    val isVideo = attachment.type == AttachmentType.Video ||
        attachment.mime.startsWith("video/") ||
        file.extension.lowercase(Locale.US) in setOf("mp4", "mkv", "webm", "avi", "3gp", "mov")

    if (attachment.isDownloaded && file.exists()) {
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
    } else {
      // Fallback to thumbnail string
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
          contentDescription = "Attachment Thumbnail",
          modifier = Modifier.fillMaxSize(),
          contentScale = ContentScale.Crop
        )
      } else {
        Box(
          modifier = Modifier.fillMaxSize().background(Color.Gray),
          contentAlignment = Alignment.Center
        ) {
          // Placeholder
        }
      }

      // Dim overlay when not downloaded
      Box(modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.3f)))
    }

    // Status Icons / Progress
    if (progress != null) {
      CircularProgressIndicator(
        progress = { progress },
        color = Color.White,
        modifier = Modifier.size(32.dp),
        trackColor = Color.White.copy(alpha = 0.3f),
      )
    } else if (!attachment.isDownloaded) {
      Icon(
        imageVector = Icons.Default.Download,
        contentDescription = "Downloading",
        tint = Color.White.copy(alpha = 0.8f),
        modifier = Modifier.size(32.dp)
      )
    } else if (isVideo) {
      Icon(
        imageVector = Icons.Default.PlayCircle,
        contentDescription = "Play Video",
        tint = Color.White.copy(alpha = 0.8f),
        modifier = Modifier.size(32.dp)
      )
    }
  }
}
