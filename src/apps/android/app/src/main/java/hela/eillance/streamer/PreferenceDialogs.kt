package hela.eillance.streamer

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp

@Composable
fun EditTextDialog(
  title: String,
  initialValue: String,
  onDismiss: () -> Unit,
  onConfirm: (String) -> Unit
) {
  var text by remember { mutableStateOf(initialValue) }

  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text(text = title) },
    text = {
      OutlinedTextField(
        value = text,
        onValueChange = { text = it },
        modifier = Modifier.fillMaxWidth()
      )
    },
    confirmButton = {
      TextButton(
        onClick = { onConfirm(text) }
      ) {
        Text(stringResource(android.R.string.ok))
      }
    },
    dismissButton = {
      TextButton(
        onClick = onDismiss
      ) {
        Text(stringResource(android.R.string.cancel))
      }
    }
  )
}

@Composable
fun ListSelectionDialog(
  title: String,
  entries: List<String>,
  entryValues: List<String>,
  selectedValue: String,
  onDismiss: () -> Unit,
  onConfirm: (String) -> Unit
) {
  var selected by remember { mutableStateOf(selectedValue) }

  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text(text = title) },
    text = {
      Column(Modifier.selectableGroup()) {
        entries.forEachIndexed { index, entry ->
          val value = entryValues.getOrNull(index) ?: return@forEachIndexed

          androidx.compose.foundation.layout.Row(
            modifier = Modifier
              .fillMaxWidth()
              .selectable(
                selected = selected == value,
                onClick = { selected = value },
                role = Role.RadioButton
              )
              .padding(vertical = 8.dp)
          ) {
            RadioButton(
              selected = selected == value,
              onClick = null // Handled by the row's selectable modifier
            )
            Text(
              text = entry,
              modifier = Modifier
                .padding(start = 8.dp)
            )
          }
        }
      }
    },
    confirmButton = {
      TextButton(
        onClick = { onConfirm(selected) }
      ) {
        Text(stringResource(android.R.string.ok))
      }
    },
    dismissButton = {
      TextButton(
        onClick = onDismiss
      ) {
        Text(stringResource(android.R.string.cancel))
      }
    }
  )
}