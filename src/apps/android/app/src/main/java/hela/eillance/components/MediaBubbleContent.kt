package hela.eillance.components

import android.graphics.BitmapFactory
import android.util.Base64
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
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

@Composable
fun MediaBubbleContent(
  attachments: List<Attachment>,
  transferProgress: Map<String, Float>,
  onMediaClick: (Attachment) -> Unit
) {
  if (attachments.isEmpty()) return

  val pagerState = rememberPagerState(pageCount = { attachments.size })

  Box(
    modifier = Modifier
      .fillMaxWidth()
      .height(200.dp) // Fixed height for the carousel
      .clip(RoundedCornerShape(8.dp))
      .background(Color.Black.copy(alpha = 0.1f))
  ) {
    HorizontalPager(
      state = pagerState,
      modifier = Modifier.fillMaxSize()
    ) { page ->
      val attachment = attachments[page]
      val progress = transferProgress[attachment.id]
      MediaItem(
        attachment = attachment,
        progress = progress,
        onClick = { if (attachment.isDownloaded) onMediaClick(attachment) }
      )
    }

    // Page indicator (if more than one)
    if (attachments.size > 1) {
      Box(
        modifier = Modifier
          .align(Alignment.BottomCenter)
          .padding(8.dp)
          .background(Color.Black.copy(alpha = 0.5f), RoundedCornerShape(12.dp))
          .padding(horizontal = 8.dp, vertical = 4.dp)
      ) {
        Text(
          text = "${pagerState.currentPage + 1}/${attachments.size}",
          color = Color.White,
          style = MaterialTheme.typography.labelSmall
        )
      }
    }
  }
}

@Composable
private fun MediaItem(attachment: Attachment, progress: Float?, onClick: () -> Unit) {
  Box(
    modifier = Modifier
      .fillMaxSize()
      .clickable(enabled = attachment.isDownloaded, onClick = onClick),
    contentAlignment = Alignment.Center
  ) {
    val file = File(attachment.path)
    val context = LocalContext.current

    // Try to display the image/video thumb from file if downloaded, otherwise fallback to thumb string
    if (attachment.isDownloaded && file.exists()) {
      val model = if (attachment.type == AttachmentType.Video) {
        ImageRequest.Builder(context)
          .data(file)
          .decoderFactory { result, options, _ -> VideoFrameDecoder(result.source, options) }
          .videoFrameMillis(1000)
          .build()
      } else {
        file
      }

      AsyncImage(
        model = model,
        contentDescription = "Attachment",
        modifier = Modifier.fillMaxSize(),
        contentScale = ContentScale.Crop
      )
    } else {
      // Fallback to embedded thumbnail
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
          // If no thumbnail, show icon
        }
      }

      // Overlay when not downloaded or downloading
      Box(modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.3f)))
    }

    // Overlays
    if (progress != null) {
      CircularProgressIndicator(
        progress = { progress },
        color = Color.White,
        modifier = Modifier.size(48.dp)
      )
    } else if (!attachment.isDownloaded) {
      Icon(
        imageVector = Icons.Default.Download,
        contentDescription = "Downloading", // Waiting to start
        tint = Color.White.copy(alpha = 0.8f),
        modifier = Modifier.size(48.dp)
      )
    } else if (attachment.type == AttachmentType.Video) {
      Icon(
        imageVector = Icons.Default.PlayCircle,
        contentDescription = "Play Video",
        tint = Color.White.copy(alpha = 0.8f),
        modifier = Modifier.size(48.dp)
      )
    }
  }
}
