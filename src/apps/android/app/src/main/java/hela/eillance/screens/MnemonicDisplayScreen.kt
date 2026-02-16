package hela.eillance.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Done
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.viewmodels.ProfileViewModel

@Composable
fun MnemonicDisplayScreen(
  profileVM: ProfileViewModel,
  onFinishSetup: () -> Unit
) {
  var mnemonic by remember { mutableStateOf<String?>(null) }

  LaunchedEffect(Unit) {
    mnemonic = profileVM.generateNewIdentity()
  }

  Surface(modifier = Modifier.fillMaxSize()) {
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(16.dp),
      horizontalAlignment = Alignment.CenterHorizontally,
      verticalArrangement = Arrangement.Center
    ) {
      if (mnemonic == null) {
        CircularProgressIndicator()
        Text(
          stringResource(id = R.string.identity_generating_progress),
          modifier = Modifier.padding(top = 16.dp)
        )
      } else {
        Icon(
          imageVector = Icons.Default.Warning,
          contentDescription = stringResource(id = R.string.warning),
          tint = MaterialTheme.colorScheme.error,
          modifier = Modifier.size(48.dp)
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text(
          stringResource(id = R.string.mnemonic_save_title),
          style = MaterialTheme.typography.headlineMedium,
          textAlign = TextAlign.Center
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
          "Write down these 12 words in a safe place. This is the only way to recover your identity if you lose your device. Do not share it with anyone.",
          style = MaterialTheme.typography.bodyMedium,
          textAlign = TextAlign.Center
        )
        Spacer(modifier = Modifier.height(24.dp))

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
        Spacer(modifier = Modifier.height(24.dp))

        Button(
          onClick = onFinishSetup,
          modifier = Modifier.fillMaxWidth()
        ) {
          Icon(Icons.Default.Done, contentDescription = null)
          Spacer(Modifier.width(8.dp))
          Text(stringResource(id = R.string.mnemonic_action_saved))
        }
      }
    }
  }
}
