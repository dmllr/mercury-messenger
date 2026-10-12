package hela.eillance.screens

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withLink
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import hela.eillance.camera.R
import hela.eillance.navigation.Route
import hela.eillance.navigation.Screen
import hela.eillance.viewmodels.ProfileViewModel
import kotlinx.coroutines.launch

private const val URL_IDENTITY =
  "https://mercury.eillance.com/articles/cryptographic-identity/embed/"
private const val URL_P2P = "https://mercury.eillance.com/articles/peer-to-peer-connections/embed/"
private const val URL_E2EE = "https://mercury.eillance.com/articles/end-to-end-encryption/embed/"

@Composable
fun IdentityScreen(navController: NavController, profileVM: ProfileViewModel) {
  val context = LocalContext.current
  val scope = rememberCoroutineScope()
  var showRestoreDialog by remember { mutableStateOf<Uri?>(null) }
  var isRestoring by remember { mutableStateOf(false) }

  val restoreLauncher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.GetContent()
  ) { uri ->
    if (uri != null) {
      showRestoreDialog = uri
    }
  }

  if (showRestoreDialog != null) {
    RestoreBackupDialog(
      onDismiss = { showRestoreDialog = null },
      onConfirm = { password ->
        scope.launch {
          isRestoring = true
          val success = profileVM.restoreBackup(showRestoreDialog!!, password)
          isRestoring = false
          if (success) {
            // Identity restored, MercuryApp will react to profileVM state change
            showRestoreDialog = null
          } else {
            Toast.makeText(
              context,
              context.getString(R.string.backup_restore_fail),
              Toast.LENGTH_LONG
            ).show()
          }
        }
      },
      isRestoring = isRestoring
    )
  }

  Surface(modifier = Modifier.fillMaxSize()) {
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(24.dp),
      horizontalAlignment = Alignment.CenterHorizontally,
      verticalArrangement = Arrangement.Center
    ) {
      Text(
        stringResource(id = R.string.identity_welcome_title),
        style = MaterialTheme.typography.headlineMedium,
        textAlign = TextAlign.Center
      )
      Spacer(modifier = Modifier.height(32.dp))

      val rawText = stringResource(id = R.string.identity_description)
      val primaryColor = MaterialTheme.colorScheme.primary

      val annotatedText = remember(rawText, primaryColor) {
        val linkStyle = SpanStyle(
          color = primaryColor,
          textDecoration = TextDecoration.Underline
        )

        buildAnnotatedString {
          // Regex matches [Link Text](tag_name)
          val regex = Regex("""\[([^\[\]]+)]\(([^()]+)\)""")
          var lastIndex = 0

          regex.findAll(rawText).forEach { matchResult ->
            append(rawText.substring(lastIndex, matchResult.range.first))

            val linkText = matchResult.groupValues[1]
            val tag = matchResult.groupValues[2]

            withLink(
              link = LinkAnnotation.Clickable(
                tag = tag,
                styles = TextLinkStyles(style = linkStyle),
                linkInteractionListener = {
                  // Map the tag from translations to your Kotlin URLs
                  val url = when (tag) {
                    "url_identity" -> URL_IDENTITY
                    "url_e2ee" -> URL_E2EE
                    "url_p2p" -> URL_P2P
                    else -> tag
                  }
                  navController.navigate(Route.WebView.createRoute(url))
                }
              )
            ) {
              append(linkText)
            }
            lastIndex = matchResult.range.last + 1
          }
          append(rawText.substring(lastIndex))
        }
      }

      SelectionContainer {
        Text(
          text = annotatedText,
          style = MaterialTheme.typography.bodyLarge.copy(
            textAlign = TextAlign.Center
          ),
        )
      }

      Spacer(modifier = Modifier.height(48.dp))
      Button(
        onClick = { navController.navigate(Screen.MnemonicDisplay.route) },
        modifier = Modifier.fillMaxWidth()
      ) {
        Text(stringResource(id = R.string.identity_action_create))
      }
      Spacer(modifier = Modifier.height(16.dp))
      OutlinedButton(
        onClick = { navController.navigate(Screen.RecoverIdentity.route) },
        modifier = Modifier.fillMaxWidth()
      ) {
        Text(stringResource(id = R.string.identity_action_recover_phrase))
      }
      Spacer(modifier = Modifier.height(16.dp))
      OutlinedButton(
        onClick = { restoreLauncher.launch("*/*") },
        modifier = Modifier.fillMaxWidth()
      ) {
        Text(stringResource(id = R.string.backup_restore_button))
      }
    }
  }
}

@Composable
private fun RestoreBackupDialog(
  onDismiss: () -> Unit,
  onConfirm: (String) -> Unit,
  isRestoring: Boolean
) {
  var password by remember { mutableStateOf("") }

  AlertDialog(
    onDismissRequest = { if (!isRestoring) onDismiss() },
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
          singleLine = true,
          enabled = !isRestoring
        )
        if (isRestoring) {
          Spacer(modifier = Modifier.height(16.dp))
          CircularProgressIndicator(modifier = Modifier.align(Alignment.CenterHorizontally))
        }
      }
    },
    confirmButton = {
      Button(
        onClick = { onConfirm(password) },
        enabled = password.isNotEmpty() && !isRestoring
      ) { Text(stringResource(id = R.string.action_restore)) }
    },
    dismissButton = {
      if (!isRestoring) {
        TextButton(onClick = onDismiss) { Text(stringResource(id = R.string.action_cancel)) }
      }
    }
  )
}
