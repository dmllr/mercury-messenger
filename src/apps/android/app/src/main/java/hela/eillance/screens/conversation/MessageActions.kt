package hela.eillance.screens.conversation

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import hela.eillance.camera.R
import hela.eillance.ndkio.Reaction

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MessageOptionsSheet(
  onDismiss: () -> Unit,
  onCopy: () -> Unit,
  onDelete: () -> Unit,
  onEdit: (() -> Unit)?, // Null if not editable
  onReact: (Reaction) -> Unit,
  currentReactions: Set<Reaction>
) {
  ModalBottomSheet(onDismissRequest = onDismiss) {
    Column(modifier = Modifier.padding(bottom = 24.dp)) {
      // Reactions Row
      Row(
        modifier = Modifier
          .fillMaxWidth()
          .padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween
      ) {
        Reaction.entries.forEach { reaction ->
          val isSelected = reaction in currentReactions
          Box(
            modifier = Modifier
              .clip(CircleShape)
              .background(
                if (isSelected) MaterialTheme.colorScheme.primaryContainer else Color.Transparent
              )
              .clickable { onReact(reaction); onDismiss() }
              .padding(8.dp)
          ) {
            Text(text = reaction.symbol, fontSize = 24.sp)
          }
        }
      }

      HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

      ActionItem(
        icon = Icons.Default.ContentCopy,
        label = stringResource(R.string.action_copy),
        onClick = { onCopy(); onDismiss() }
      )

      if (onEdit != null) {
        ActionItem(
          icon = Icons.Default.Edit,
          label = stringResource(R.string.action_edit),
          onClick = { onEdit(); onDismiss() }
        )
      }

      ActionItem(
        icon = Icons.Default.Delete,
        label = stringResource(R.string.action_delete),
        color = MaterialTheme.colorScheme.error,
        onClick = { onDelete(); onDismiss() }
      )
    }
  }
}

@Composable
fun EditMessageDialog(
  initialContent: String,
  onDismiss: () -> Unit,
  onConfirm: (String) -> Unit
) {
  var content by remember { mutableStateOf(initialContent) }

  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text(stringResource(R.string.action_edit)) },
    text = {
      OutlinedTextField(
        value = content,
        onValueChange = { content = it },
        modifier = Modifier.fillMaxWidth(),
        maxLines = 10
      )
    },
    confirmButton = {
      Button(
        onClick = { onConfirm(content) },
        enabled = content.isNotBlank() && content != initialContent
      ) {
        Text(stringResource(R.string.action_save))
      }
    },
    dismissButton = {
      TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) }
    }
  )
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

@Composable
private fun ActionItem(
  icon: ImageVector,
  label: String,
  onClick: () -> Unit,
  color: Color = MaterialTheme.colorScheme.onSurface
) {
  ListItem(
    headlineContent = { Text(label, color = color) },
    leadingContent = { Icon(icon, contentDescription = null, tint = color) },
    modifier = Modifier.clickable(onClick = onClick)
  )
}
