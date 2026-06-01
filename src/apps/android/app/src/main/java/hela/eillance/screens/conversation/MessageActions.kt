package hela.eillance.screens.conversation

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Done
import androidx.compose.material.icons.filled.DoneAll
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupProperties
import hela.eillance.camera.R
import hela.eillance.ndkio.Reaction
import hela.eillance.theme.ListItem
import hela.eillance.theme.extendedColors

@Composable
fun MessageActionMenu(
  onDismiss: () -> Unit,
  onCopy: () -> Unit,
  onDelete: () -> Unit,
  onEdit: (() -> Unit)?,
  onShare: () -> Unit,
  onForward: () -> Unit,
  onReact: (Reaction) -> Unit,
  currentReactions: Set<Reaction>,
  isFromMe: Boolean,
  peerStatuses: List<hela.eillance.ndkio.PeerMessageStatus>,
  resolveContactName: (String) -> String
) {
  // Use Popup to position near the anchor (the Box in MessageBubble)
  // Alignment BottomStart or BottomEnd depending on message direction
  val alignment = if (isFromMe) Alignment.TopEnd else Alignment.TopStart

  Popup(
    alignment = alignment,
    offset = androidx.compose.ui.unit.IntOffset(0, 20),
    onDismissRequest = onDismiss,
    properties = PopupProperties(focusable = true)
  ) {
    Surface(
      shape = RoundedCornerShape(16.dp),
      shadowElevation = 8.dp,
      tonalElevation = 4.dp,
      color = MaterialTheme.colorScheme.surfaceContainer,
      modifier = Modifier
        .width(IntrinsicSize.Max)
        .widthIn(min = 200.dp, max = 300.dp)
        .padding(4.dp)
    ) {
      Column {
        // Reactions
        Row(
          modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 8.dp, vertical = 12.dp),
          horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
          Reaction.entries.forEach { reaction ->
            val isSelected = reaction in currentReactions
            Box(
              modifier = Modifier
                .clip(CircleShape)
                .background(
                  if (isSelected) MaterialTheme.colorScheme.primaryContainer else Color.Transparent
                )
                .clickable { onReact(reaction) }
                .padding(6.dp)
            ) {
              Text(text = reaction.symbol, fontSize = 24.sp)
            }
          }
        }

        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

        // Actions
        Column(modifier = Modifier.padding(vertical = 4.dp)) {
          ActionItem(
            icon = Icons.Default.ContentCopy,
            label = stringResource(R.string.action_copy),
            onClick = onCopy
          )

          ActionItem(
            icon = Icons.Default.Share,
            label = stringResource(R.string.action_share),
            onClick = onShare
          )

          ActionItem(
            icon = Icons.AutoMirrored.Filled.ArrowForward,
            label = stringResource(R.string.action_forward),
            onClick = onForward
          )

          if (onEdit != null) {
            ActionItem(
              icon = Icons.Default.Edit,
              label = stringResource(R.string.action_edit),
              onClick = onEdit
            )
          }

          ActionItem(
            icon = Icons.Default.Delete,
            label = stringResource(R.string.action_delete),
            color = MaterialTheme.colorScheme.error,
            onClick = onDelete
          )
        }

        if (isFromMe && peerStatuses.isNotEmpty()) {
          HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
          Column(modifier = Modifier.padding(vertical = 4.dp)) {
            Text(stringResource(R.string.message_read_by), style = MaterialTheme.typography.labelSmall, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
            peerStatuses.forEach { ps ->
              val name = resolveContactName(ps.peerId)
              val icon = when(ps.status) {
                hela.eillance.ndkio.MessageStatus.Read -> Icons.Filled.DoneAll
                hela.eillance.ndkio.MessageStatus.Delivered -> Icons.Filled.Done
                else -> Icons.Filled.Schedule
              }
              val tint = if (ps.status == hela.eillance.ndkio.MessageStatus.Read) MaterialTheme.extendedColors.success else MaterialTheme.colorScheme.onSurfaceVariant
              ListItem(
                headlineContent = { Text(name, style = MaterialTheme.typography.bodyMedium) },
                trailingContent = { Icon(icon, contentDescription = null, tint = tint, modifier = Modifier.size(16.dp)) },
                modifier = Modifier.height(36.dp),
                colors = androidx.compose.material3.ListItemDefaults.colors(containerColor = Color.Transparent)
              )
            }
          }
        }
      }
    }
  }
}

@Composable
private fun ActionItem(
  icon: ImageVector,
  label: String,
  onClick: () -> Unit,
  color: Color = MaterialTheme.colorScheme.onSurface
) {
  ListItem(
    headlineContent = { Text(label, color = color, style = MaterialTheme.typography.bodyMedium) },
    leadingContent = { Icon(icon, contentDescription = null, tint = color) },
    modifier = Modifier
      .clickable(onClick = onClick)
      .height(48.dp), // Compact height
    colors = androidx.compose.material3.ListItemDefaults.colors(containerColor = Color.Transparent)
  )
}

@Composable
fun MessageEditor(
  initialContent: String,
  onCommit: (String) -> Unit,
  onCancel: () -> Unit,
  modifier: Modifier = Modifier
) {
  var content by remember { mutableStateOf(initialContent) }

  Box(modifier = modifier) {
    OutlinedTextField(
      value = content,
      onValueChange = { content = it },
      modifier = Modifier
        .fillMaxWidth()
        .padding(bottom = 24.dp) // Space for floating buttons
        .background(MaterialTheme.colorScheme.surface, MaterialTheme.shapes.medium),
      shape = MaterialTheme.shapes.medium,
      textStyle = MaterialTheme.typography.bodyLarge
    )

    // Overlapping Buttons
    Row(
      modifier = Modifier
        .align(Alignment.BottomEnd)
        .offset(y = 12.dp, x = (-8).dp), // Slight offset overlap
      horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
      IconButton(
        onClick = onCancel,
        modifier = Modifier
          .size(40.dp)
          .shadow(4.dp, CircleShape)
          .background(MaterialTheme.colorScheme.surfaceContainerHigh, CircleShape)
          .border(1.dp, MaterialTheme.colorScheme.outlineVariant, CircleShape)
      ) {
        Icon(
          Icons.Default.Close,
          contentDescription = stringResource(R.string.action_cancel),
          tint = MaterialTheme.colorScheme.error
        )
      }

      IconButton(
        onClick = { if (content.isNotBlank()) onCommit(content) },
        enabled = content.isNotBlank(),
        modifier = Modifier
          .size(40.dp)
          .shadow(4.dp, CircleShape)
          .background(MaterialTheme.colorScheme.primaryContainer, CircleShape)
          .border(1.dp, MaterialTheme.colorScheme.primary, CircleShape)
      ) {
        Icon(
          Icons.Default.Check,
          contentDescription = stringResource(R.string.action_save),
          tint = MaterialTheme.colorScheme.onPrimaryContainer
        )
      }
    }
  }
}

@Composable
fun DeleteMessageDialog(
  onDismiss: () -> Unit,
  onConfirm: () -> Unit
) {
  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text(stringResource(R.string.delete_message_title)) },
    text = { Text(stringResource(R.string.delete_message_confirmation)) },
    confirmButton = {
      Button(
        onClick = onConfirm,
        colors = androidx.compose.material3.ButtonDefaults.buttonColors(
          containerColor = MaterialTheme.colorScheme.error
        )
      ) {
        Text(stringResource(R.string.action_delete))
      }
    },
    dismissButton = {
      TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) }
    }
  )
}
