package hela.eillance.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentStatus
import hela.eillance.ndkio.AttachmentType

@Composable
fun MediaBubbleContent(
  attachments: List<Attachment>,
  transferProgress: Map<String, Float>,
  isFromMe: Boolean,
  selectionMode: Boolean = false,
  onMediaClick: (Attachment) -> Unit,
  onLongClick: (() -> Unit)? = null,
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
        selectionMode = selectionMode,
        onMediaClick = onMediaClick,
        onLongClick = onLongClick,
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
            selectionMode = selectionMode,
            onClick = { onMediaClick(file) },
            onLongClick = onLongClick,
            onCancel = { onCancel(file.id) },
            onResume = { onResume(file.id) },
            onDiscard = { onDiscard(file.id) }
          )
        }
      }
    }
  }
}
