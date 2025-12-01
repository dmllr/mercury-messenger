package hela.eillance.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@Composable
fun BetaNoticeDialog(onDismiss: () -> Unit) {
  AlertDialog(
    onDismissRequest = onDismiss,
    icon = { Icon(Icons.Default.Info, contentDescription = null) },
    title = { Text("Welcome to Mercury Beta") },
    text = {
      Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
        Text("Mercury is currently in active development. You may experience bugs or incomplete features.")
        Spacer(modifier = Modifier.height(16.dp))
        Text("Already there:", fontWeight = FontWeight.Bold)
        Text("• Secure text messages")
        Text("• Secure voice and video calls")
        Spacer(modifier = Modifier.height(8.dp))
        Text("Coming soon:", fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(8.dp))
        Text("• Support for other languages")
        Text("• Image and Video Messages")
        Text("• Group Chats")
        Text("• Public Channels")
        Text("• Secure File Sharing")
        Text("• Voice Messages")
        Text("• Ephemeral (Disappearing) Messages")
        Spacer(modifier = Modifier.height(16.dp))
        Text("Thank you for using it!")
      }
    },
    confirmButton = {
      TextButton(onClick = onDismiss) {
        Text("I Understand")
      }
    },
    modifier = Modifier.fillMaxWidth()
  )
}
