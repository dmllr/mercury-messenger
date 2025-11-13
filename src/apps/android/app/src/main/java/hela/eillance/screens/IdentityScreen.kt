package hela.eillance.screens

import android.net.Uri
import androidx.browser.customtabs.CustomTabsIntent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.ClickableText
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment.Companion.CenterHorizontally
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import hela.eillance.navigation.Screen

private const val URL_IDENTITY = "https://mercury.eillance.com/articles/3/"
private const val URL_P2P = "https://mercury.eillance.com/articles/2/"
private const val URL_E2EE = "https://mercury.eillance.com/articles/4/"

@Composable
fun IdentityScreen(navController: NavController) {
  Surface(modifier = Modifier.fillMaxSize()) {
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(24.dp),
      horizontalAlignment = CenterHorizontally,
      verticalArrangement = Arrangement.Center
    ) {
      Text("Welcome to Mercury", style = MaterialTheme.typography.headlineLarge)
      Spacer(modifier = Modifier.height(16.dp))

      val annotatedText = buildAnnotatedString {
        append("Mercury provides you with a secure ")
        pushStringAnnotation(tag = "identity", annotation = URL_IDENTITY)
        withStyle(style = SpanStyle(color = MaterialTheme.colorScheme.primary, textDecoration = TextDecoration.Underline)) {
          append("cryptographic identity")
        }
        pop()
        append(". Your messages are protected with ")
        pushStringAnnotation(tag = "e2ee", annotation = URL_E2EE)
        withStyle(style = SpanStyle(color = MaterialTheme.colorScheme.primary, textDecoration = TextDecoration.Underline)) {
          append("end-to-end encryption")
        }
        pop()
        append(" and sent directly between devices via ")
        pushStringAnnotation(tag = "p2p", annotation = URL_P2P)
        withStyle(style = SpanStyle(color = MaterialTheme.colorScheme.primary, textDecoration = TextDecoration.Underline)) {
          append("peer-to-peer")
        }
        pop()
        append(" connections.")
      }

      val context = LocalContext.current
      val toolbarColor = MaterialTheme.colorScheme.surfaceVariant
      val customTabsIntent = remember {
        CustomTabsIntent.Builder()
          .setToolbarColor(toolbarColor.toArgb())
          .build()
      }

      ClickableText(
        text = annotatedText,
        // The fix is to explicitly set a theme-aware color for the text style.
        style = MaterialTheme.typography.bodyLarge.copy(
          color = MaterialTheme.colorScheme.onSurface,
          textAlign = TextAlign.Center
        ),
        onClick = { offset ->
          annotatedText.getStringAnnotations(tag = "identity", start = offset, end = offset).firstOrNull()?.let {
            customTabsIntent.launchUrl(context, Uri.parse(it.item))
          }
          annotatedText.getStringAnnotations(tag = "e2ee", start = offset, end = offset).firstOrNull()?.let {
            customTabsIntent.launchUrl(context, Uri.parse(it.item))
          }
          annotatedText.getStringAnnotations(tag = "p2p", start = offset, end = offset).firstOrNull()?.let {
            customTabsIntent.launchUrl(context, Uri.parse(it.item))
          }
        }
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
