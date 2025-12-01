package hela.eillance.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.CallEnd
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import hela.eillance.theme.extendedColors

@Composable
fun IncomingCallScreen(
  callerId: String,
  onAccept: () -> Unit,
  onReject: () -> Unit
) {
  Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(16.dp),
      horizontalAlignment = Alignment.CenterHorizontally,
      verticalArrangement = Arrangement.SpaceAround
    ) {
      Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text("Incoming Call", style = MaterialTheme.typography.headlineMedium)
        Spacer(modifier = Modifier.height(16.dp))
        Text(callerId, style = MaterialTheme.typography.titleLarge)
      }

      Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceEvenly
      ) {
        FloatingActionButton(
          onClick = onReject,
          containerColor = MaterialTheme.colorScheme.error
        ) {
          Icon(Icons.Default.CallEnd, contentDescription = "Reject", tint = Color.White)
        }
        FloatingActionButton(
          onClick = onAccept,
          containerColor = MaterialTheme.extendedColors.success
        ) {
          Icon(Icons.Default.Call, contentDescription = "Accept", tint = Color.White)
        }
      }
    }
  }
}
