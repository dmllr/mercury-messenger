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
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import hela.eillance.viewmodels.ProfileViewModel
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RecoverIdentityScreen(
  navController: NavController,
  profileVM: ProfileViewModel
) {
  var mnemonic by remember { mutableStateOf("") }
  var error by remember { mutableStateOf<String?>(null) }
  var isLoading by remember { mutableStateOf(false) }
  val scope = rememberCoroutineScope()

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text("Recover Identity") },
        navigationIcon = {
          IconButton(onClick = { navController.popBackStack() }) {
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
      Text("Enter your 12-word recovery phrase below, separated by spaces.")
      Spacer(modifier = Modifier.height(16.dp))
      OutlinedTextField(
        value = mnemonic,
        onValueChange = { mnemonic = it; error = null },
        label = { Text("Recovery Phrase") },
        modifier = Modifier
          .fillMaxWidth()
          .height(120.dp),
        isError = error != null,
        supportingText = { if (error != null) Text(error!!) }
      )
      Spacer(modifier = Modifier.height(24.dp))
      Button(
        onClick = {
          scope.launch {
            isLoading = true
            error = null
            val words = mnemonic.trim().split(Regex("\\s+"))
            if (words.size != 12) {
              error = "Please enter exactly 12 words."
              isLoading = false
            } else {
              val success = profileVM.restoreIdentity(mnemonic.trim())
              if (!success) {
                error = "Invalid recovery phrase. Please check the words and try again."
                isLoading = false
              }
              // On success, the MainViewModel state will change, and the NavHost in MercuryApp will automatically switch screens.
            }
          }
        },
        enabled = !isLoading && mnemonic.isNotBlank(),
        modifier = Modifier.fillMaxWidth()
      ) {
        if (isLoading) {
          CircularProgressIndicator(modifier = Modifier.size(24.dp), color = MaterialTheme.colorScheme.onPrimary)
        } else {
          Text("Recover")
        }
      }
    }
  }
}
