package hela.eillance.screens

import androidx.browser.customtabs.CustomTabsIntent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withLink
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.navigation.NavController
import hela.eillance.navigation.Screen

private const val URL_IDENTITY = "https://mercury.eillance.com/articles/cryptographic-identity/embed/"
private const val URL_P2P = "https://mercury.eillance.com/articles/peer-to-peer-connections/embed/"
private const val URL_E2EE = "https://mercury.eillance.com/articles/end-to-end-encryption/embed/"

@Composable
fun IdentityScreen(navController: NavController) {
  Surface(modifier = Modifier.fillMaxSize()) {
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(24.dp),
      horizontalAlignment = Alignment.CenterHorizontally,
      verticalArrangement = Arrangement.Center
    ) {
      Text("Welcome to Mercury", style = MaterialTheme.typography.headlineLarge)
      Spacer(modifier = Modifier.height(16.dp))

      val context = LocalContext.current
      val toolbarColor = MaterialTheme.colorScheme.surfaceVariant
      val customTabsIntent = remember {
        CustomTabsIntent.Builder()
          .setToolbarColor(toolbarColor.toArgb())
          .build()
      }

      val linkStyle = SpanStyle(
        color = MaterialTheme.colorScheme.primary,
        textDecoration = TextDecoration.Underline
      )

      val annotatedText = buildAnnotatedString {
        append("Mercury provides you with a secure ")
        withLink(
          link = LinkAnnotation.Clickable(
            tag = URL_IDENTITY,
            styles = TextLinkStyles(style = linkStyle),
            linkInteractionListener = {
              customTabsIntent.launchUrl(context, URL_IDENTITY.toUri())
            }
          )
        ) {
          append("cryptographic identity")
        }
        append(". Your messages are protected with ")
        withLink(
          link = LinkAnnotation.Clickable(
            tag = URL_E2EE,
            styles = TextLinkStyles(style = linkStyle),
            linkInteractionListener = {
              customTabsIntent.launchUrl(context, URL_E2EE.toUri())
            }
          )
        ) {
          append("end-to-end encryption")
        }
        append(" and sent directly between devices via ")
        withLink(
          link = LinkAnnotation.Clickable(
            tag = URL_P2P,
            styles = TextLinkStyles(style = linkStyle),
            linkInteractionListener = {
              customTabsIntent.launchUrl(context, URL_P2P.toUri())
            }
          )
        ) {
          append("peer-to-peer")
        }
        append(" connections.")
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
