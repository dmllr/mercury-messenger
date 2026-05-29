package hela.eillance.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R

@Composable
fun CreateChannelDialog(
  onDismissRequest: () -> Unit,
  onCreateChannel: (String, String) -> Unit
) {
  var name by remember { mutableStateOf("") }
  var desc by remember { mutableStateOf("") }

  AlertDialog(
    onDismissRequest = onDismissRequest,
    title = { Text(stringResource(id = R.string.channel_new)) },
    text = {
      Column {
        OutlinedTextField(value = name, onValueChange = { name = it }, label = { Text(stringResource(id = R.string.channel_name_label)) }, singleLine = true, modifier = Modifier.fillMaxWidth())
        Spacer(modifier = Modifier.height(16.dp))
        OutlinedTextField(value = desc, onValueChange = { desc = it }, label = { Text(stringResource(id = R.string.channel_desc_label)) }, modifier = Modifier.fillMaxWidth(), maxLines = 3)
      }
    },
    confirmButton = {
      TextButton(
        onClick = { if (name.isNotBlank()) { onCreateChannel(name, desc); onDismissRequest() } },
        enabled = name.isNotBlank()
      ) { Text(stringResource(id = R.string.channel_create_action)) }
    },
    dismissButton = {
      TextButton(onClick = onDismissRequest) {
        Text(stringResource(id = R.string.action_cancel))
      }
    }
  )
}
