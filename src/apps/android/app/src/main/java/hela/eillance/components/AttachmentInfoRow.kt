package hela.eillance.components

import android.text.format.Formatter
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.InsertDriveFile
import androidx.compose.material.icons.filled.AudioFile
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PictureAsPdf
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.VideoFile
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentStatus

/**
 * Reusable component for displaying attachment info (icon, name, size/status) and controls.
 * Used for both Files (in a card) and Media (as an overlay).
 */
@Composable
fun AttachmentInfoRow(
  attachment: Attachment,
  progress: Float?,
  isFromMe: Boolean,
  onCancel: () -> Unit,
  onResume: () -> Unit,
  onDiscard: () -> Unit,
  textColor: Color,
  subTextColor: Color,
  iconTint: Color,
  iconBackground: Color,
  modifier: Modifier = Modifier
) {
  val context = LocalContext.current
  val icon = getFileIcon(attachment.mime)
  val fileSizeStr = Formatter.formatShortFileSize(context, attachment.size)
  val isDone = attachment.status == AttachmentStatus.Done
  val isCanceled =
    attachment.status == AttachmentStatus.Canceled || attachment.status == AttachmentStatus.PeerCanceled
  val isPaused = attachment.status == AttachmentStatus.Paused
  val isPending = attachment.status == AttachmentStatus.Pending
  val isDeleted = isDone && !java.io.File(attachment.path).exists()

  val hasNote = attachment.note.isNotEmpty()
  val displayName = attachment.fileName.ifEmpty { getGenericFileName(attachment.mime) }

  Row(
    modifier = modifier,
    verticalAlignment = Alignment.CenterVertically
  ) {
    // File Icon
    Box(
      modifier = Modifier
        .size(40.dp)
        .background(iconBackground, CircleShape),
      contentAlignment = Alignment.Center
    ) {
      Icon(
        imageVector = icon,
        contentDescription = null,
        tint = iconTint,
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
        color = textColor
      )
      if (hasNote) {
        Text(
          text = attachment.note,
          style = MaterialTheme.typography.bodySmall,
          color = subTextColor,
          maxLines = 1,
          overflow = TextOverflow.Ellipsis
        )
      }
      Row(verticalAlignment = Alignment.CenterVertically) {
        // Status Row (Size | Status)
        val statusText = when {
          isCanceled -> stringResource(id = R.string.file_status_canceled)
          isPaused -> stringResource(id = R.string.file_status_paused)
          isPending -> stringResource(id = R.string.file_status_waiting)
          isDeleted -> "Deleted"
          else -> fileSizeStr
        }

        val statusColor = if (isCanceled || isDeleted) MaterialTheme.colorScheme.error else subTextColor

        Text(
          text = statusText,
          style = MaterialTheme.typography.labelSmall,
          color = statusColor
        )
      }
    }

    // Action Button
    // We only show controls if not done, or if needed to retry/resume
    if (!isDone) {
      Spacer(modifier = Modifier.width(8.dp))
      TransferControl(
        attachment = attachment,
        progress = progress,
        isFromMe = isFromMe,
        onCancel = onCancel,
        onResume = onResume,
        onDiscard = onDiscard,
        tint = iconTint
      )
    }
  }
}

@Composable
private fun TransferControl(
  attachment: Attachment,
  progress: Float?,
  isFromMe: Boolean,
  onCancel: () -> Unit,
  onResume: () -> Unit,
  onDiscard: () -> Unit,
  tint: Color
) {
  val isTransferring = attachment.status == AttachmentStatus.Transferring
  val isPaused = attachment.status == AttachmentStatus.Paused
  val isPending = attachment.status == AttachmentStatus.Pending
  val isDone = attachment.status == AttachmentStatus.Done
  val isFailed = attachment.status == AttachmentStatus.Failed
  val isCanceled =
    attachment.status == AttachmentStatus.Canceled || attachment.status == AttachmentStatus.PeerCanceled

  Box(contentAlignment = Alignment.Center) {
    if (isTransferring && progress != null) {
      CircularProgressIndicator(
        progress = { progress },
        modifier = Modifier.size(32.dp),
        strokeWidth = 3.dp,
        color = tint,
        trackColor = tint.copy(alpha = 0.3f)
      )
      IconButton(onClick = onCancel, modifier = Modifier.size(24.dp)) {
        val icon = if (!isFromMe) Icons.Default.Pause else Icons.Default.Close
        Icon(
          icon,
          contentDescription = stringResource(id = R.string.action_cancel),
          modifier = Modifier.size(16.dp),
          tint = tint
        )
      }
    } else if (isPaused) {
      Row {
        if (!isFromMe) {
          IconButton(onClick = onResume, modifier = Modifier.size(32.dp)) {
            Icon(
              Icons.Default.Refresh,
              contentDescription = stringResource(id = R.string.action_resume),
              tint = tint
            )
          }
        }
        IconButton(onClick = onDiscard, modifier = Modifier.size(32.dp)) {
          Icon(
            Icons.Default.Close,
            contentDescription = stringResource(id = R.string.action_cancel),
            tint = MaterialTheme.colorScheme.error
          )
        }
      }
    } else if (isFailed) {
      IconButton(onClick = onResume, modifier = Modifier.size(32.dp)) {
        Icon(
          Icons.Default.Refresh,
          contentDescription = stringResource(id = R.string.action_retry),
          tint = tint
        )
      }
    } else if (isPending) {
      CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 2.dp, color = tint)
    } else if (!isDone && !isCanceled) {
      if (!isFromMe && !attachment.isDownloaded) {
        IconButton(onClick = onResume, modifier = Modifier.size(32.dp)) {
          Icon(
            Icons.Default.Download,
            contentDescription = stringResource(id = R.string.file_status_downloading),
            tint = tint
          )
        }
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
