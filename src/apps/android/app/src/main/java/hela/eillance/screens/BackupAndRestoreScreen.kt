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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import hela.eillance.camera.R
import hela.eillance.viewmodels.ProfileViewModel
import kotlinx.coroutines.launch
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BackupAndRestoreScreen(
  profileVM: ProfileViewModel,
  onBack: () -> Unit,
  onNavigateToRecoveryPhrase: () -> Unit,
  onRestoreSuccess: () -> Unit
) {
  var showBackupDialog by remember { mutableStateOf(false) }
  var showRestoreConfirmation by remember { mutableStateOf<Uri?>(null) }
  val snackbarHostState = remember { SnackbarHostState() }
  val scope = rememberCoroutineScope()
  val context = LocalContext.current
  var showViewPhraseDialog by remember { mutableStateOf(false) }
  var passwordForBackup by remember { mutableStateOf<String?>(null) }

  val restoreLauncher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.GetContent()
  ) { uri: Uri? ->
    if (uri != null) {
      showRestoreConfirmation = uri
    }
  }

  val createBackupLauncher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.CreateDocument("application/octet-stream")
  ) { uri: Uri? ->
    uri?.let { destinationUri ->
      passwordForBackup?.let { password ->
        scope.launch {
          val tempBackupPath = profileVM.createBackupToTemp(password)
          if (tempBackupPath != null) {
            try {
              context.contentResolver.openOutputStream(destinationUri)?.use { outputStream ->
                File(tempBackupPath).inputStream().use { inputStream ->
                  inputStream.copyTo(outputStream)
                }
              }
              snackbarHostState.showSnackbar(context.getString(R.string.backup_save_success))
            } catch (e: Exception) {
              snackbarHostState.showSnackbar(context.getString(R.string.backup_save_fail))
            } finally {
              profileVM.deleteTempBackup(tempBackupPath)
            }
          } else {
            snackbarHostState.showSnackbar(context.getString(R.string.backup_create_fail))
          }
        }
      }
    }
    passwordForBackup = null
  }

  if (showBackupDialog) {
    BackupPasswordDialog(
      onDismiss = { showBackupDialog = false },
      onConfirm = { password ->
        showBackupDialog = false
        passwordForBackup = password
        val timestamp = SimpleDateFormat("yyyy-MM-dd-HHmmss", Locale.US).format(Date())
        createBackupLauncher.launch("mercury-backup-$timestamp.mcr")
      }
    )
  }

  showRestoreConfirmation?.let { uri ->
    RestorePasswordDialog(
      onDismiss = { showRestoreConfirmation = null },
      onConfirm = { password ->
        scope.launch {
          val success = profileVM.restoreBackup(uri, password)
          if (success) {
            onRestoreSuccess()
          } else {
            snackbarHostState.showSnackbar(context.getString(R.string.backup_restore_fail))
          }
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
        title = { Text(stringResource(id = R.string.screen_title_backup_restore)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(id = R.string.action_back))
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
      Text(stringResource(id = R.string.backup_create_title), style = MaterialTheme.typography.titleLarge)
      Text(
        stringResource(id = R.string.backup_create_description),
        style = MaterialTheme.typography.bodyMedium,
        modifier = Modifier.padding(top = 4.dp, bottom = 16.dp)
      )
      Button(onClick = { showBackupDialog = true }) {
        Icon(Icons.Default.Save, contentDescription = null)
        Spacer(Modifier.width(8.dp))
        Text(stringResource(id = R.string.backup_create_button))
      }

      HorizontalDivider(modifier = Modifier.padding(vertical = 24.dp))

      Text(stringResource(id = R.string.backup_restore_title), style = MaterialTheme.typography.titleLarge)
      Text(
        stringResource(id = R.string.backup_restore_description),
        style = MaterialTheme.typography.bodyMedium,
        modifier = Modifier.padding(top = 4.dp, bottom = 16.dp)
      )
      Button(onClick = { restoreLauncher.launch("*/*") }) {
        Text(stringResource(id = R.string.backup_restore_button))
      }

      HorizontalDivider(modifier = Modifier.padding(vertical = 24.dp))

      Text(stringResource(id = R.string.backup_view_phrase_title), style = MaterialTheme.typography.titleLarge)
      Text(
        stringResource(id = R.string.backup_view_phrase_description),
        style = MaterialTheme.typography.bodyMedium,
        modifier = Modifier.padding(top = 4.dp, bottom = 16.dp)
      )
      OutlinedButton(
        onClick = { showViewPhraseDialog = true },
        colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.secondary)
      ) {
        Text(stringResource(id = R.string.backup_view_phrase_button))
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
        Text(stringResource(id = R.string.backup_password_dialog_title), style = MaterialTheme.typography.headlineSmall)
        Spacer(modifier = Modifier.height(16.dp))
        OutlinedTextField(
          value = password,
          onValueChange = { password = it },
          label = { Text(stringResource(id = R.string.password_label)) },
          visualTransformation = PasswordVisualTransformation(),
          singleLine = true
        )
        Spacer(modifier = Modifier.height(8.dp))
        OutlinedTextField(
          value = confirmPassword,
          onValueChange = { confirmPassword = it },
          label = { Text(stringResource(id = R.string.password_confirm_label)) },
          visualTransformation = PasswordVisualTransformation(),
          singleLine = true,
          isError = confirmPassword.isNotEmpty() && password != confirmPassword
        )
        Spacer(modifier = Modifier.height(24.dp))
        Row(
          modifier = Modifier.fillMaxWidth(),
          horizontalArrangement = Arrangement.End
        ) {
          TextButton(onClick = onDismiss) { Text(stringResource(id = R.string.action_cancel)) }
          Button(onClick = { onConfirm(password); onDismiss() }, enabled = isMatch) {
            Text(stringResource(id = R.string.action_create))
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
    title = { Text(stringResource(id = R.string.restore_password_dialog_title)) },
    text = {
      Column {
        Text(stringResource(id = R.string.restore_password_dialog_prompt))
        Spacer(modifier = Modifier.height(16.dp))
        OutlinedTextField(
          value = password,
          onValueChange = { password = it },
          label = { Text(stringResource(id = R.string.password_label)) },
          visualTransformation = PasswordVisualTransformation(),
          singleLine = true
        )
      }
    },
    confirmButton = {
      Button(
        onClick = { onConfirm(password); onDismiss() },
        enabled = password.isNotEmpty()
      ) { Text(stringResource(id = R.string.action_restore)) }
    },
    dismissButton = {
      TextButton(onClick = onDismiss) { Text(stringResource(id = R.string.action_cancel)) }
    }
  )
}

@Composable
private fun ViewPhraseConfirmationDialog(onDismiss: () -> Unit, onConfirm: () -> Unit) {
  AlertDialog(
    onDismissRequest = onDismiss,
    icon = {
      Icon(
        Icons.Default.Warning,
        contentDescription = stringResource(id = R.string.warning),
        modifier = Modifier.size(48.dp)
      )
    },
    title = { Text(stringResource(id = R.string.view_phrase_dialog_title)) },
    text = {
      Text(
        stringResource(id = R.string.view_phrase_dialog_prompt),
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
          Text(stringResource(id = R.string.action_show_phrase))
        }
      }
    },
    dismissButton = {
      TextButton(onClick = onDismiss) {
        Text(stringResource(id = R.string.action_cancel))
      }
    }
  )
}
