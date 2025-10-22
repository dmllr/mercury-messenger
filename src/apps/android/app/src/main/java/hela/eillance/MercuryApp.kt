package hela.eillance

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import hela.eillance.navigation.Route
import hela.eillance.navigation.Screen
import hela.eillance.screens.ContactsScreen
import hela.eillance.screens.InteractionScreen
import hela.eillance.screens.ProfileScreen

@Composable
fun MercuryApp(viewModel: MainViewModel) {
    val navController = rememberNavController()
    val uiState by viewModel.uiState.collectAsState()

    if (viewModel.showAddContactDialog.collectAsState().value) {
        AddContactDialog(
            onDismissRequest = { viewModel.onAddContactDialogDismiss() },
            onConfirm = { peerId -> viewModel.addContact(peerId) }
        )
    }

    NavHost(
        navController = navController,
        startDestination = Screen.Contacts.route,
    ) {
        composable(Screen.Contacts.route) {
            ContactsScreen(
                viewModel = viewModel,
                uiState = uiState,
                onContactClick = { peerId ->
                    viewModel.selectContact(peerId)
                    navController.navigate(Route.Interaction.createRoute(peerId))
                },
                onNavigateToProfile = {
                    navController.navigate(Screen.Profile.route)
                }
            )
        }
        composable(Screen.Profile.route) {
            ProfileScreen(
                myId = uiState.myId,
                onBack = { navController.popBackStack() }
            )
        }
        composable(Route.Interaction.route) {
            InteractionScreen(
                viewModel = viewModel,
                uiState = uiState,
                onBack = { navController.popBackStack() }
            )
        }
    }
}
