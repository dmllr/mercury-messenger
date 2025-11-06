package hela.eillance.screens

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import hela.eillance.MainViewModel
import kotlinx.coroutines.launch
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BackupAndRestoreScreen(
  viewModel: MainViewModel,
  onBack: () -> Unit,
  onNavigateToRecoveryPhrase: () -> Unit
) {
  var showBackupDialog by remember { mutableStateOf(false) }
  var showRestoreConfirmation by remember { mutableStateOf<Uri?>(null) }
  val snackbarHostState = remember { SnackbarHostState() }
  val scope = rememberCoroutineScope()
  val context = LocalContext.current
  var showViewPhraseDialog by remember { mutableStateOf(false) }

  val restoreLauncher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.GetContent()
  ) { uri: Uri? ->
    if (uri != null) {
      showRestoreConfirmation = uri
    }
  }

  if (showBackupDialog) {
    BackupPasswordDialog(
      onDismiss = { showBackupDialog = false },
      onConfirm = { password ->
        scope.launch {
          val backupsDir = context.getExternalFilesDir("backups")
          if (backupsDir != null && (backupsDir.exists() || backupsDir.mkdirs())) {
            val timestamp = SimpleDateFormat("yyyy-MM-dd-HHmmss", Locale.US).format(Date())
            val backupFile = File(backupsDir, "mercury-backup-$timestamp.mcr")
            val success = viewModel.createBackup(backupFile.absolutePath, password)
            snackbarHostState.showSnackbar(
              if (success) "Backup created successfully." else "Backup failed."
            )
          } else {
            snackbarHostState.showSnackbar("Could not access backup directory.")
          }
        }
      }
    )
  }

  showRestoreConfirmation?.let { uri ->
    RestorePasswordDialog(
      onDismiss = { showRestoreConfirmation = null },
      onConfirm = { password ->
        scope.launch {
          val success = viewModel.restoreBackup(uri, password)
          snackbarHostState.showSnackbar(
            if (success) "Restore successful. Please restart the app." else "Restore failed. Check password or file."
          )
        }
      }
    )
  }

  if (showViewPhraseDialog) {
    ViewPhraseConfirmationDialog(
      onDismiss = { showViewPhraseDialog = false },
      onConfirm = {
        showViewPhraseDialog = false
        onNavigateToRecoveryPhrase()
      }
    )
  }

  Scaffold(
    snackbarHost = { SnackbarHost(snackbarHostState) },
    topBar = {
      TopAppBar(
        title = { Text("Backup & Restore") },
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
        .padding(16.dp)
    ) {
      Text("Create Full Backup", style = MaterialTheme.typography.titleLarge)
      Text(
        "Create an encrypted backup of your identity, contacts, and message history. Keep the backup file and your password safe.",
        style = MaterialTheme.typography.bodyMedium,
        modifier = Modifier.padding(top = 4.dp, bottom = 16.dp)
      )
      Button(onClick = { showBackupDialog = true }) {
        Icon(Icons.Default.Save, contentDescription = null)
        Spacer(Modifier.width(8.dp))
        Text("Create Encrypted Backup")
      }

      HorizontalDivider(modifier = Modifier.padding(vertical = 24.dp))

      Text("Restore Full Backup", style = MaterialTheme.typography.titleLarge)
      Text(
        "Restore from an encrypted backup file. This will overwrite all current data on this device.",
        style = MaterialTheme.typography.bodyMedium,
        modifier = Modifier.padding(top = 4.dp, bottom = 16.dp)
      )
      Button(onClick = { restoreLauncher.launch("*/*") }) {
        Text("Restore from Backup File")
      }

      HorizontalDivider(modifier = Modifier.padding(vertical = 24.dp))

      Text("View Recovery Phrase", style = MaterialTheme.typography.titleLarge)
      Text(
        "Display the 12-word phrase used to recover your identity only. Do this in a private location.",
        style = MaterialTheme.typography.bodyMedium,
        modifier = Modifier.padding(top = 4.dp, bottom = 16.dp)
      )
      OutlinedButton(
        onClick = { showViewPhraseDialog = true },
        colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.secondary)
      ) {
        Text("Show Recovery Phrase")
      }
    }
  }
}

@Composable
private fun BackupPasswordDialog(onDismiss: () -> Unit, onConfirm: (String) -> Unit) {
  var password by remember { mutableStateOf("") }
  var confirmPassword by remember { mutableStateOf("") }
  val isMatch = password.isNotEmpty() && password == confirmPassword

  Dialog(onDismissRequest = onDismiss) {
    Card {
      Column(modifier = Modifier.padding(24.dp)) {
        Text("Set Backup Password", style = MaterialTheme.typography.headlineSmall)
        Spacer(modifier = Modifier.height(16.dp))
        OutlinedTextField(
          value = password,
          onValueChange = { password = it },
          label = { Text("Password") },
          visualTransformation = PasswordVisualTransformation(),
          singleLine = true
        )
        Spacer(modifier = Modifier.height(8.dp))
        OutlinedTextField(
          value = confirmPassword,
          onValueChange = { confirmPassword = it },
          label = { Text("Confirm Password") },
          visualTransformation = PasswordVisualTransformation(),
          singleLine = true,
          isError = confirmPassword.isNotEmpty() && password != confirmPassword
        )
        Spacer(modifier = Modifier.height(24.dp))
        Row(
          modifier = Modifier.fillMaxWidth(),
          horizontalArrangement = Arrangement.End
        ) {
          TextButton(onClick = onDismiss) { Text("Cancel") }
          Button(onClick = { onConfirm(password); onDismiss() }, enabled = isMatch) {
            Text("Create")
          }
        }
      }
    }
  }
}

@Composable
private fun RestorePasswordDialog(onDismiss: () -> Unit, onConfirm: (String) -> Unit) {
  var password by remember { mutableStateOf("") }

  AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text("Enter Backup Password") },
    text = {
      Column {
        Text("Enter the password for the selected backup file to restore your data.")
        Spacer(modifier = Modifier.height(16.dp))
        OutlinedTextField(
          value = password,
          onValueChange = { password = it },
          label = { Text("Password") },
          visualTransformation = PasswordVisualTransformation(),
          singleLine = true
        )
      }
    },
    confirmButton = {
      Button(
        onClick = { onConfirm(password); onDismiss() },
        enabled = password.isNotEmpty()
      ) { Text("Restore") }
    },
    dismissButton = {
      TextButton(onClick = onDismiss) { Text("Cancel") }
    }
  )
}

@Composable
private fun ViewPhraseConfirmationDialog(onDismiss: () -> Unit, onConfirm: () -> Unit) {
  AlertDialog(
    onDismissRequest = onDismiss,
    icon = { Icon(Icons.Default.Warning, contentDescription = "Warning", modifier = Modifier.size(48.dp)) },
    title = { Text("Reveal Recovery Phrase?") },
    text = {
      Text(
        "Anyone who sees your recovery phrase can steal your identity. \n\nAre you sure you want to proceed?",
        textAlign = TextAlign.Center
      )
    },
    confirmButton = {
      Button(
        onClick = onConfirm,
        colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
      ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          Icon(Icons.Default.Visibility, contentDescription = null)
          Spacer(Modifier.width(8.dp))
          Text("Show Phrase")
        }
      }
    },
    dismissButton = {
      TextButton(onClick = onDismiss) {
        Text("Cancel")
      }
    }
  )
}
