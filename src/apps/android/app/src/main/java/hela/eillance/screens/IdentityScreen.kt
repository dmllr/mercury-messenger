package hela.eillance.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import hela.eillance.navigation.Screen

@Composable
fun IdentityScreen(navController: NavController) {
  Surface(modifier = Modifier.fillMaxSize()) {
    Column(
      modifier = Modifier.fillMaxSize().padding(24.dp),
      horizontalAlignment = Alignment.CenterHorizontally,
      verticalArrangement = Arrangement.Center
    ) {
      Text("Welcome to Mercury", style = MaterialTheme.typography.headlineLarge)
      Spacer(modifier = Modifier.height(16.dp))
      Text(
        "A private and secure messenger. Your identity is generated and stored only on your device.",
        style = MaterialTheme.typography.bodyLarge,
        textAlign = TextAlign.Center
      )
      Spacer(modifier = Modifier.height(48.dp))
      Button(
        onClick = { navController.navigate(Screen.MnemonicDisplay.route) },
        modifier = Modifier.fillMaxWidth()
      ) {
        Text("Create New Identity")
      }
      Spacer(modifier = Modifier.height(16.dp))
      OutlinedButton(
        onClick = { navController.navigate(Screen.RecoverIdentity.route) },
        modifier = Modifier.fillMaxWidth()
      ) {
        Text("Restore from Recovery Phrase")
      }
    }
  }
}
