package hela.eillance

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue

@Composable
fun AddContactDialog(
  onDismissRequest: () -> Unit,
  onConfirm: (String) -> Unit
) {
  var peerId by remember { mutableStateOf("") }

  AlertDialog(
    onDismissRequest = onDismissRequest,
    title = { Text("Add Contact") },
    text = {
      TextField(
        value = peerId,
        onValueChange = { peerId = it },
        label = { Text("Peer ID") },
        singleLine = true
      )
    },
    confirmButton = {
      TextButton(
        onClick = {
          if (peerId.isNotBlank()) {
            onConfirm(peerId)
          }
        }
      ) {
        Text("Add")
      }
    },
    dismissButton = {
      TextButton(onClick = onDismissRequest) {
        Text("Cancel")
      }
    }
  )
}
