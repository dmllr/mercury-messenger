package hela.eillance.navigation

import android.net.Uri
import androidx.annotation.StringRes
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Help
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.Contacts
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Settings
import androidx.compose.ui.graphics.vector.ImageVector
import hela.eillance.camera.R
import hela.eillance.ndkio.Attachment
import kotlinx.serialization.json.Json

sealed class Screen(val route: String, @StringRes val titleResId: Int? = null, val icon: ImageVector? = null) {
  object Contacts : Screen("contacts", R.string.screen_title_contacts, Icons.Default.Contacts)
  object Profile : Screen("profile", R.string.screen_title_profile, Icons.Default.Person)
  object Settings : Screen("settings", R.string.screen_title_settings, Icons.Default.Settings)
  object BackupRestore : Screen("backup_restore", R.string.screen_title_backup_restore)
  object Identity : Screen("identity")
  object MnemonicDisplay : Screen("mnemonic_display")
  object RecoverIdentity : Screen("recover_identity", R.string.screen_title_recover_identity)
  object RecoveryPhrase : Screen("recovery_phrase", R.string.screen_title_recovery_phrase)
  object Help : Screen("help", R.string.screen_title_help, Icons.AutoMirrored.Filled.Help)
  object QrScanner : Screen("qr_scanner", R.string.screen_title_qr_scanner)
  object RejectedContacts : Screen("rejected_contacts", R.string.screen_title_rejected_contacts)
  object PermissionIssues : Screen("permission_issues", R.string.screen_title_permission_issues)

  // Debug
  object DebugMenu : Screen("debug_menu", R.string.screen_title_debug_menu, Icons.Default.BugReport)
  object DebugPendingActions : Screen("debug_pending_actions", R.string.screen_title_pending_actions)
  object DebugConnections : Screen("debug_connections", R.string.screen_title_connections)
  object DebugUnread : Screen("debug_unread", R.string.screen_title_unread_messages)
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
  object FullScreenMedia : Route("media_viewer/{peerId}?attachments={attachments}&index={index}") {
    fun createRoute(peerId: String, attachments: List<Attachment>, initialIndex: Int): String {
      val json = Json.encodeToString(attachments)
      return "media_viewer/$peerId?attachments=${Uri.encode(json)}&index=$initialIndex"
    }
  }
}
