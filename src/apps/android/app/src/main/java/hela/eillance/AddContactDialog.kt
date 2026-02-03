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
import androidx.compose.ui.res.stringResource
import hela.eillance.camera.R
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
    title = { Text(stringResource(id = R.string.add_contact_title)) },
    text = {
      Column {
        Text(stringResource(id = R.string.add_contact_prompt))
        Spacer(Modifier.height(16.dp))
        TextField(
          value = peerId,
          onValueChange = { peerId = it },
          label = { Text(stringResource(id = R.string.add_contact_peer_id_label)) },
          singleLine = true,
          modifier = Modifier.fillMaxWidth()
        )
        Spacer(Modifier.height(8.dp))
        OutlinedButton(
          onClick = onScanQrCode,
          modifier = Modifier.fillMaxWidth()
        ) {
          Icon(Icons.Default.QrCodeScanner, contentDescription = stringResource(id = R.string.add_contact_scan_qr))
          Spacer(modifier = Modifier.width(8.dp))
          Text(stringResource(id = R.string.add_contact_scan_qr))
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
        Text(stringResource(id = R.string.action_add))
      }
    },
    dismissButton = {
      TextButton(onClick = onDismissRequest) {
        Text(stringResource(id = R.string.action_cancel))
      }
    }
  )
}
