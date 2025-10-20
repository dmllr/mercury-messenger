package hela.eillance

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import hela.eillance.navigation.Route
import hela.eillance.navigation.Screen
import hela.eillance.screens.CallsScreen
import hela.eillance.screens.ChatScreen
import hela.eillance.screens.ChatsScreen
import hela.eillance.screens.ContactsScreen
import hela.eillance.screens.ProfileScreen

@Composable
fun MercuryApp(viewModel: MainViewModel) {
    val navController = rememberNavController()
    val bottomNavItems = listOf(Screen.Chats, Screen.Calls, Screen.Contacts, Screen.Profile)

    Scaffold(
        bottomBar = {
            NavigationBar {
                val navBackStackEntry by navController.currentBackStackEntryAsState()
                val currentDestination = navBackStackEntry?.destination
                bottomNavItems.forEach { screen ->
                    NavigationBarItem(
                        icon = { Icon(screen.icon, contentDescription = screen.title) },
                        label = { Text(screen.title) },
                        selected = currentDestination?.hierarchy?.any { it.route == screen.route } == true,
                        onClick = {
                            navController.navigate(screen.route) {
                                popUpTo(navController.graph.findStartDestination().id) {
                                    saveState = true
                                }
                                launchSingleTop = true
                                restoreState = true
                            }
                        }
                    )
                }
            }
        }
    ) { innerPadding ->
        val uiState by viewModel.uiState.collectAsState()

        NavHost(
            navController,
            startDestination = Screen.Chats.route,
            Modifier.padding(innerPadding)
        ) {
            composable(Screen.Chats.route) {
                ChatsScreen(
                    chats = uiState.chats,
                    onChatClick = { peerId ->
                        viewModel.selectContact(peerId)
                        navController.navigate(Route.Chat.createRoute(peerId))
                    }
                )
            }
            composable(Screen.Calls.route) {
                CallsScreen(calls = uiState.calls)
            }
            composable(Screen.Contacts.route) {
                ContactsScreen(
                    viewModel = viewModel,
                    contacts = uiState.contacts,
                    onContactClick = { peerId ->
                        viewModel.selectContact(peerId)
                        navController.navigate(Route.Chat.createRoute(peerId))
                    }
                )
            }
            composable(Screen.Profile.route) {
                ProfileScreen(myId = uiState.my_id)
            }
            composable(Route.Chat.route) {
                ChatScreen(
                    uiState = uiState,
                    onSendMessage = viewModel::sendMessage,
                    onLoadOlder = viewModel::loadOlderMessages,
                    onBack = { navController.popBackStack() }
                )
            }
        }
    }
}
