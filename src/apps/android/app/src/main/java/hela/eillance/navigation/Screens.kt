package hela.eillance.navigation

import android.net.Uri
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Help
import androidx.compose.material.icons.filled.Contacts
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.ui.graphics.vector.ImageVector
import hela.eillance.ndkio.Attachment
import kotlinx.serialization.json.Json

sealed class Screen(val route: String, val icon: ImageVector? = null) {
  object Contacts : Screen("contacts", Icons.Default.Contacts)
  object Profile : Screen("profile", Icons.Default.Person)
  object Settings : Screen("settings", Icons.Default.Settings)
  object ConnectionSettings : Screen("connection_settings", Icons.Default.Wifi)
  object BackupRestore : Screen("backup_restore")
  object Identity : Screen("identity")
  object MnemonicDisplay : Screen("mnemonic_display")
  object RecoverIdentity : Screen("recover_identity")
  object RecoveryPhrase : Screen("recovery_phrase")
  object Help : Screen("help", Icons.AutoMirrored.Filled.Help)
  object QrScanner : Screen("qr_scanner")
  object RejectedContacts : Screen("rejected_contacts")
  object PermissionIssues : Screen("permission_issues")
}

sealed class Route(val route: String) {
  object Interaction : Route("interaction/{peerId}?messageId={messageId}") {
    fun createRoute(peerId: String, messageId: Long? = null) =
      if (messageId != null) "interaction/$peerId?messageId=$messageId" else "interaction/$peerId"
  }

  object ConversationMedia : Route("conversation_media/{peerId}") {
    fun createRoute(peerId: String) = "conversation_media/$peerId"
  }

  object Call : Route("call/{peerId}") {
    fun createRoute(peerId: String) = "call/$peerId"
  }

  object WebView : Route("webview/{url}") {
    fun createRoute(url: String): String = "webview/${Uri.encode(url)}"
  }

  object FullScreenMedia : Route("media_viewer/{peerId}?attachments={attachments}&index={index}") {
    fun createRoute(peerId: String, attachments: List<Attachment>, initialIndex: Int): String {
      // Strip thumbnails to keep URL short and avoid TransactionTooLargeException and parsing issues
      val lightweightAttachments = attachments.map { it.copy(thumbnail = null) }
      val json = Json.encodeToString(lightweightAttachments)
      return "media_viewer/$peerId?attachments=${Uri.encode(json)}&index=$initialIndex"
    }
  }
}
