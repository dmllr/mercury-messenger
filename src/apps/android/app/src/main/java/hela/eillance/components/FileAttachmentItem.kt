package hela.eillance.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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

@Composable
fun FileAttachmentItem(
  attachment: Attachment,
  progress: Float?,
  isFromMe: Boolean,
  selectionMode: Boolean,
  onClick: () -> Unit,
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
      .clickable(enabled = isDone && !selectionMode, onClick = onClick)
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
