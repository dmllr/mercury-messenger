package hela.eillance.navigation

import android.net.Uri
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Help
import androidx.compose.material.icons.filled.Contacts
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Settings
import androidx.compose.ui.graphics.vector.ImageVector

sealed class Screen(val route: String, val title: String? = null, val icon: ImageVector? = null) {
  object Contacts : Screen("contacts", "Contacts", Icons.Default.Contacts)
  object Profile : Screen("profile", "Profile", Icons.Default.Person)
  object Settings : Screen("settings", "Settings", Icons.Default.Settings)
  object BackupRestore : Screen("backup_restore", "Backup & Restore")
  object Identity : Screen("identity")
  object MnemonicDisplay : Screen("mnemonic_display")
  object RecoverIdentity : Screen("recover_identity", "Recover Identity")
  object RecoveryPhrase : Screen("recovery_phrase", "Recovery Phrase")
  object Help : Screen("help", "Help", Icons.AutoMirrored.Filled.Help)
  object QrScanner : Screen("qr_scanner", "Scan QR Code")
  object RejectedContacts : Screen("rejected_contacts", "Blocked Contacts")
}

sealed class Route(val route: String) {
  object Interaction : Route("interaction/{peerId}") {
    fun createRoute(peerId: String) = "interaction/$peerId"
  }
  object Call : Route("call/{peerId}") {
    fun createRoute(peerId: String) = "call/$peerId"
  }
  object WebView : Route("webview/{url}") {
    fun createRoute(url: String): String = "webview/${Uri.encode(url)}"
  }
  object FullScreenMedia : Route("media_viewer?path={path}&type={type}&mime={mime}") {
    fun createRoute(path: String, type: Int, mime: String): String {
      return "media_viewer?path=${Uri.encode(path)}&type=$type&mime=${Uri.encode(mime)}"
    }
  }
}
