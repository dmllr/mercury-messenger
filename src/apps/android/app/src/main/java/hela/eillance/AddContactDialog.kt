package hela.eillance

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.QrCodeScanner
import androidx.compose.material3.Icon
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.material3.OutlinedButton
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun AddContactDialog(
  onDismissRequest: () -> Unit,
  onConfirm: (String) -> Unit,
  onScanQrCode: () -> Unit,
) {
  var peerId by remember { mutableStateOf("") }

  AlertDialog(
    onDismissRequest = onDismissRequest,
    title = { Text("Add Contact") },
    text = {
      Column {
        Text("Enter the Peer ID below, or scan their QR code.")
        Spacer(Modifier.height(16.dp))
        TextField(
          value = peerId,
          onValueChange = { peerId = it },
          label = { Text("Peer ID") },
          singleLine = true,
          modifier = Modifier.fillMaxWidth()
        )
        Spacer(Modifier.height(8.dp))
        OutlinedButton(
          onClick = onScanQrCode,
          modifier = Modifier.fillMaxWidth()
        ) {
          Icon(Icons.Default.QrCodeScanner, contentDescription = "Scan QR Code")
          Spacer(modifier = Modifier.width(8.dp))
          Text("Scan QR Code")
        }
      }
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
