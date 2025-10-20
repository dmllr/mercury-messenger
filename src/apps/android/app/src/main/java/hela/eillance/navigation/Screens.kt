package hela.eillance.navigation

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.Contacts
import androidx.compose.material.icons.filled.Message
import androidx.compose.material.icons.filled.Person
import androidx.compose.ui.graphics.vector.ImageVector

sealed class Screen(val route: String, val title: String, val icon: ImageVector) {
    object Chats : Screen("chats", "Chats", Icons.Default.Message)
    object Calls : Screen("calls", "Calls", Icons.Default.Call)
    object Contacts : Screen("contacts", "Contacts", Icons.Default.Contacts)
    object Profile : Screen("profile", "Profile", Icons.Default.Person)
}

sealed class Route(val route: String) {
    object Chat : Route("chat/{peerId}") {
        fun createRoute(peerId: String) = "chat/$peerId"
    }
}
