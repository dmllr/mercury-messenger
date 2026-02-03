package hela.eillance.components

import android.graphics.BitmapFactory
import android.text.format.Formatter
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
import androidx.compose.material.icons.automirrored.filled.InsertDriveFile
import androidx.compose.material.icons.filled.AudioFile
import androidx.compose.material.icons.filled.Block
import androidx.compose.material.icons.filled.BrokenImage
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PictureAsPdf
import androidx.compose.material.icons.filled.PlayCircle
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.VideoFile
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import hela.eillance.camera.R
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

  // Split into Visuals (Image/Video) and Files (Docs/Binary)
  val (visuals, files) = visibleAttachments.partition {
    it.type == AttachmentType.Image || it.type == AttachmentType.Video
  }

  Column(modifier = Modifier.fillMaxWidth()) {
    // 1. Render Visuals Grid
    if (visuals.isNotEmpty()) {
      VisualAttachmentsGrid(
        attachments = visuals,
        transferProgress = transferProgress,
        isFromMe = isFromMe,
        onMediaClick = onMediaClick,
        onCancel = onCancel,
        onResume = onResume,
        onDiscard = onDiscard
      )
    }

    // 2. Render Files List
    if (files.isNotEmpty()) {
      // Add spacer if we have both types
      if (visuals.isNotEmpty()) {
        Spacer(modifier = Modifier.height(4.dp))
      }
      Column(
        verticalArrangement = Arrangement.spacedBy(4.dp),
        modifier = Modifier.fillMaxWidth()
      ) {
        files.forEach { file ->
          FileAttachmentItem(
            attachment = file,
            progress = transferProgress[file.id],
            isFromMe = isFromMe,
            onClick = { onMediaClick(file) },
            onCancel = { onCancel(file.id) },
            onResume = { onResume(file.id) },
            onDiscard = { onDiscard(file.id) }
          )
        }
      }
    }
  }
}

@Composable
private fun VisualAttachmentsGrid(
  attachments: List<Attachment>,
  transferProgress: Map<String, Float>,
  isFromMe: Boolean,
  onMediaClick: (Attachment) -> Unit,
  onCancel: (String) -> Unit,
  onResume: (String) -> Unit,
  onDiscard: (String) -> Unit
) {
  val count = attachments.size
  val roundedShape = RoundedCornerShape(8.dp)

  val renderItem: @Composable (Int, Modifier) -> Unit = { index, modifier ->
    val att = attachments[index]
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
                    .clickable { onMediaClick(attachments[3]) },
                  contentAlignment = Alignment.Center
                ) {
                  Text(text = stringResource(id = R.string.plus_more, count - 4), style = MaterialTheme.typography.titleLarge, color = Color.White)
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
private fun FileAttachmentItem(
  attachment: Attachment,
  progress: Float?,
  isFromMe: Boolean,
  onClick: () -> Unit,
  onCancel: () -> Unit,
  onResume: () -> Unit,
  onDiscard: () -> Unit
) {
  val context = LocalContext.current
  val icon = getFileIcon(attachment.mime)
  val fileSizeStr = Formatter.formatShortFileSize(context, attachment.size)
  val isTransferring = attachment.status == AttachmentStatus.Transferring
  val isPending = attachment.status == AttachmentStatus.Pending
  val isPaused = attachment.status == AttachmentStatus.Paused
  val isDone = attachment.status == AttachmentStatus.Done
  val isCanceled = attachment.status == AttachmentStatus.Canceled || attachment.status == AttachmentStatus.PeerCanceled

  val displayName = attachment.fileName.ifEmpty {
    attachment.note.ifEmpty {
      getGenericFileName(attachment.mime)
    }
  }

  Row(
    modifier = Modifier
      .fillMaxWidth()
      .clip(RoundedCornerShape(8.dp))
      .background(MaterialTheme.colorScheme.surfaceContainerHigh)
      .clickable(enabled = isDone, onClick = onClick)
      .padding(8.dp),
    verticalAlignment = Alignment.CenterVertically
  ) {
    // File Icon
    Box(
      modifier = Modifier
        .size(40.dp)
        .background(MaterialTheme.colorScheme.surface, CircleShape),
      contentAlignment = Alignment.Center
    ) {
      Icon(
        imageVector = icon,
        contentDescription = null,
        tint = MaterialTheme.colorScheme.primary,
        modifier = Modifier.size(24.dp)
      )
    }

    Spacer(modifier = Modifier.width(12.dp))

    // Info
    Column(modifier = Modifier.weight(1f)) {
      Text(
        text = displayName,
        style = MaterialTheme.typography.bodyMedium,
        fontWeight = FontWeight.Medium,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        color = MaterialTheme.colorScheme.onSurface
      )
      Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
          text = fileSizeStr,
          modifier = Modifier.padding(end = 8.dp),
          style = MaterialTheme.typography.labelSmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        if (isCanceled) {
          Text(text = stringResource(id = R.string.file_status_canceled), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.error)
        } else if (isPaused) {
          Text(text = stringResource(id = R.string.file_status_paused), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.tertiary)
        } else if (isPending) {
          Text(text = stringResource(id = R.string.file_status_waiting), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
      }
    }

    // Action Button
    Box(contentAlignment = Alignment.Center) {
      if (isTransferring && progress != null) {
        CircularProgressIndicator(
          progress = { progress },
          modifier = Modifier.size(32.dp),
          strokeWidth = 3.dp,
          color = MaterialTheme.colorScheme.primary
        )
        IconButton(onClick = onCancel, modifier = Modifier.size(24.dp)) {
          val icon = if (!isFromMe) Icons.Default.Pause else Icons.Default.Close
          Icon(icon, contentDescription = stringResource(id = R.string.action_cancel), modifier = Modifier.size(16.dp))
        }
      } else if (isPaused) {
        Row {
          // Only Receiver can Resume
          if (!isFromMe) {
            IconButton(onClick = onResume, modifier = Modifier.size(32.dp)) {
              Icon(Icons.Default.Refresh, contentDescription = stringResource(id = R.string.action_resume), tint = MaterialTheme.colorScheme.primary)
            }
          }
          // Both can Cancel/Discard completely
          IconButton(onClick = onDiscard, modifier = Modifier.size(32.dp)) {
            Icon(Icons.Default.Close, contentDescription = stringResource(id = R.string.action_cancel), tint = MaterialTheme.colorScheme.error)
          }
        }
      } else if (attachment.status == AttachmentStatus.Failed) {
        IconButton(onClick = onResume) {
          Icon(Icons.Default.Refresh, contentDescription = stringResource(id = R.string.action_retry), tint = MaterialTheme.colorScheme.primary)
        }
      } else if (isPending) {
        CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 2.dp)
      } else if (!isDone && !isCanceled) {
        if (!isFromMe && !attachment.isDownloaded) {
          Icon(Icons.Default.Download, contentDescription = "Downloading", tint = MaterialTheme.colorScheme.secondary)
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
  val isCanceled = attachment.status == AttachmentStatus.Canceled || attachment.status == AttachmentStatus.PeerCanceled

  Box(
    modifier = modifier
      .background(MaterialTheme.colorScheme.surfaceContainerHigh)
      .clickable(
        enabled = attachment.status == AttachmentStatus.Done,
        onClick = onClick
      ),
    contentAlignment = Alignment.Center
  ) {
    val file = File(attachment.path)
    val context = LocalContext.current
    val isVideo = attachment.type == AttachmentType.Video || attachment.mime.startsWith("video/")

    // --- BASE LAYER (Thumbnail/Image/Video) ---
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
          } catch (e: Exception) { null }
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
        Box(modifier = Modifier.fillMaxSize().background(MaterialTheme.colorScheme.surfaceVariant), contentAlignment = Alignment.Center) {
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
    // Show overlay for all non-final states
    if (attachment.status != AttachmentStatus.Done) {
      Box(modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.5f)))
    }

    // --- INTERACTIVE / STATUS LAYER ---
    Box(modifier = Modifier.fillMaxSize()) {
      if (isCanceled) {
        Column(
          modifier = Modifier.align(Alignment.Center),
          horizontalAlignment = Alignment.CenterHorizontally,
          verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
          Icon(
            imageVector = Icons.Default.Block,
            contentDescription = stringResource(id = R.string.file_status_canceled),
            tint = Color.White,
            modifier = Modifier.size(32.dp)
          )
          Text(
            text = if (attachment.status == AttachmentStatus.Canceled) stringResource(id = R.string.upload_canceled) else stringResource(id = R.string.download_canceled),
            style = MaterialTheme.typography.labelSmall,
            color = Color.White
          )
        }
      } else if (attachment.status == AttachmentStatus.Paused) {
        Column(
          modifier = Modifier.align(Alignment.Center),
          horizontalAlignment = Alignment.CenterHorizontally,
          verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
          if (!isFromMe) {
            Icon(
              imageVector = Icons.Default.Refresh,
              contentDescription = stringResource(id = R.string.action_resume),
              tint = Color.White,
              modifier = Modifier.size(32.dp).clickable { onResume() }
            )
          }
          Text(stringResource(id = R.string.file_status_paused), style = MaterialTheme.typography.labelSmall, color = Color.White)
        }
        IconButton(
          onClick = onDiscard,
          modifier = Modifier.align(Alignment.TopEnd).padding(4.dp)
        ) {
          Icon(Icons.Default.Close, contentDescription = stringResource(id = R.string.action_cancel), tint = Color.White)
        }
      } else if (progress != null && progress >= 0f && attachment.status == AttachmentStatus.Transferring) {
        Box(
          modifier = Modifier.align(Alignment.Center),
          contentAlignment = Alignment.Center
        ) {
          CircularProgressIndicator(
            progress = { progress },
            modifier = Modifier.size(48.dp),
            color = Color.White,
            trackColor = Color.White.copy(alpha = 0.4f),
          )
          IconButton(onClick = onCancel) {
            val icon = if (!isFromMe) Icons.Default.Pause else Icons.Default.Close
            Icon(icon, contentDescription = stringResource(id = R.string.action_cancel), tint = Color.White)
          }
        }
      } else if (attachment.status == AttachmentStatus.Pending || (attachment.status == AttachmentStatus.Transferring && progress == null)) {
        CircularProgressIndicator(
          modifier = Modifier.size(32.dp).align(Alignment.Center),
          color = Color.White
        )
      } else if (attachment.status == AttachmentStatus.Failed) {
        Icon(
          Icons.Default.Refresh,
          contentDescription = stringResource(id = R.string.action_retry),
          tint = MaterialTheme.colorScheme.error,
          modifier = Modifier.size(32.dp).align(Alignment.Center).clickable { onResume() }
        )
      } else if (isVideo && attachment.status == AttachmentStatus.Done) {
        Icon(
          imageVector = Icons.Default.PlayCircle,
          contentDescription = stringResource(id = R.string.action_play),
          tint = Color.White.copy(alpha = 0.8f),
          modifier = Modifier.size(48.dp).align(Alignment.Center)
        )
      }
    }
  }
}

private fun getFileIcon(mimeType: String): ImageVector {
  return when {
    mimeType == "application/pdf" -> Icons.Default.PictureAsPdf
    mimeType.startsWith("audio/") -> Icons.Default.AudioFile
    mimeType.startsWith("video/") -> Icons.Default.VideoFile
    mimeType.startsWith("image/") -> Icons.Default.Image
    mimeType.contains("word") || mimeType.contains("document") || mimeType.contains("text") -> Icons.Default.Description
    else -> Icons.AutoMirrored.Filled.InsertDriveFile
  }
}

@Composable
private fun getGenericFileName(mimeType: String): String {
  return when {
    mimeType == "application/pdf" -> stringResource(id = R.string.file_type_pdf)
    mimeType.startsWith("audio/") -> stringResource(id = R.string.file_type_audio)
    mimeType.startsWith("video/") -> stringResource(id = R.string.file_type_video)
    mimeType.startsWith("image/") -> stringResource(id = R.string.file_type_image)
    else -> stringResource(id = R.string.file_type_generic)
  }
}
