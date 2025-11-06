package hela.eillance.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import hela.eillance.utils.rememberImageBitmapFromBase64
import hela.eillance.ndkio.Contact

@Composable
fun ContactProfileDialog(
    contact: Contact,
    onDismissRequest: () -> Unit,
    onOverrideSave: (String) -> Unit,
    onOverrideReset: () -> Unit,
    onRemoveRequest: () -> Unit
) {
    var overrideName by remember { mutableStateOf("") }

    Dialog(onDismissRequest = onDismissRequest) {
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            shape = MaterialTheme.shapes.large,
        ) {
            Column(
                modifier = Modifier.padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                val imageBitmap = rememberImageBitmapFromBase64(contact.photo)
                if (imageBitmap != null) {
                    Image(
                        bitmap = imageBitmap,
                        contentDescription = "Profile Photo",
                        modifier = Modifier
                            .size(120.dp)
                            .clip(CircleShape),
                        contentScale = ContentScale.Crop
                    )
                } else {
                    Icon(
                        imageVector = Icons.Default.Person,
                        contentDescription = "Default Profile Icon",
                        modifier = Modifier
                            .size(120.dp)
                            .clip(CircleShape)
                            .background(MaterialTheme.colorScheme.secondaryContainer)
                            .padding(24.dp)
                    )
                }

                Spacer(modifier = Modifier.height(16.dp))

                Text(text = contact.name, style = MaterialTheme.typography.headlineSmall)
                Text(text = contact.peerId, style = MaterialTheme.typography.bodyMedium)

                contact.bio?.let {
                    if (it.isNotEmpty()) {
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(text = it, style = MaterialTheme.typography.bodyLarge)
                    }
                }

                Spacer(modifier = Modifier.height(24.dp))
                HorizontalDivider()
                Spacer(modifier = Modifier.height(16.dp))

                Text("Set Local Alias", style = MaterialTheme.typography.titleMedium)
                Spacer(modifier = Modifier.height(8.dp))

                OutlinedTextField(
                    value = overrideName,
                    onValueChange = { overrideName = it },
                    label = { Text("Local name for this contact") },
                    singleLine = true
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    TextButton(onClick = {
                        onOverrideReset()
                        onDismissRequest()
                    }) {
                        Text("Reset")
                    }
                    Button(
                        onClick = {
                            onOverrideSave(overrideName)
                            onDismissRequest()
                        },
                        enabled = overrideName.isNotBlank()
                    ) {
                        Text("Save Alias")
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))
                HorizontalDivider()
                Spacer(modifier = Modifier.height(16.dp))

                Button(
                    onClick = {
                        onRemoveRequest()
                        onDismissRequest()
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("Remove Contact")
                }
            }
        }
    }
}
