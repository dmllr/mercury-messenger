package hela.eillance.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import hela.eillance.MainViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RecoveryPhraseScreen(viewModel: MainViewModel, onBack: () -> Unit) {
  val mnemonic by remember { mutableStateOf(viewModel.getMnemonic()) }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text("Recovery Phrase") },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
          }
        }
      )
    }
  ) { padding ->
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
        .padding(16.dp),
      horizontalAlignment = Alignment.CenterHorizontally
    ) {
      Icon(
        imageVector = Icons.Default.Warning,
        contentDescription = "Warning",
        tint = MaterialTheme.colorScheme.error,
        modifier = Modifier.size(48.dp)
      )
      Spacer(modifier = Modifier.height(16.dp))
      Text(
        "Do Not Share This Phrase",
        style = MaterialTheme.typography.headlineSmall,
        textAlign = TextAlign.Center,
        color = MaterialTheme.colorScheme.error
      )
      Spacer(modifier = Modifier.height(8.dp))
      Text(
        "Anyone with these words can take control of your identity. Keep them secret and secure.",
        style = MaterialTheme.typography.bodyMedium,
        textAlign = TextAlign.Center
      )
      Spacer(modifier = Modifier.height(24.dp))

      if (mnemonic != null) {
        Card(
          modifier = Modifier.fillMaxWidth(),
          colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
        ) {
          Text(
            text = mnemonic!!,
            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(24.dp)
          )
        }
      } else {
        Text("Could not retrieve recovery phrase. It may not have been stored correctly.")
      }
    }
  }
}
