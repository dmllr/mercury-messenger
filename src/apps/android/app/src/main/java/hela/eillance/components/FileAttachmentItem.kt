package hela.eillance.components

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentStatus

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun FileAttachmentItem(
  attachment: Attachment,
  progress: Float?,
  isFromMe: Boolean,
  selectionMode: Boolean,
  onClick: () -> Unit,
  onLongClick: (() -> Unit)? = null,
  onCancel: () -> Unit,
  onResume: () -> Unit,
  onDiscard: () -> Unit
) {
  val isDone = attachment.status == AttachmentStatus.Done

  Box(
    modifier = Modifier
      .fillMaxWidth()
      .clip(RoundedCornerShape(8.dp))
      .background(MaterialTheme.colorScheme.surfaceContainerHigh)
      .combinedClickable(
        enabled = isDone || selectionMode,
        onClick = {
          if (selectionMode) onLongClick?.invoke() else onClick()
        },
        onLongClick = { onLongClick?.invoke() }
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
      textColor = MaterialTheme.colorScheme.onSurface,
      subTextColor = MaterialTheme.colorScheme.onSurfaceVariant,
      iconTint = MaterialTheme.colorScheme.primary,
      iconBackground = MaterialTheme.colorScheme.surface
    )
  }
}
