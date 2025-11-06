package hela.eillance.navigation

import androidx.compose.material.icons.Icons
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
}

sealed class Route(val route: String) {
    object Interaction : Route("interaction/{peerId}") {
        fun createRoute(peerId: String) = "interaction/$peerId"
    }
    object Call : Route("call/{peerId}") {
        fun createRoute(peerId: String) = "call/$peerId"
    }
}
