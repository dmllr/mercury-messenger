package hela.eillance

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.CallEnd
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import hela.eillance.navigation.Route
import hela.eillance.navigation.Screen
import hela.eillance.screens.CallScreen
import hela.eillance.screens.ContactsScreen
import hela.eillance.screens.InteractionScreen
import hela.eillance.screens.ProfileScreen
import hela.eillance.screens.SettingsScreen

@Composable
fun MercuryApp(viewModel: MainViewModel) {
    val navController = rememberNavController()
    val uiState by viewModel.uiState.collectAsState()
    val navigateToPeerId by viewModel.navigateToPeerId.collectAsState()

    LaunchedEffect(navigateToPeerId) {
        navigateToPeerId?.let { peerId ->
            // Ensure we are not on the call screen when navigating to a chat
            if (!uiState.callState.isInCall && !uiState.callState.isRinging) {
                navController.navigate(Route.Interaction.createRoute(peerId)) {
                    // This is key: If we are already on the target screen, don't create a new instance.
                    launchSingleTop = true
                }
            }
            viewModel.onNavigationHandled()
        }
    }

    if (viewModel.showAddContactDialog.collectAsState().value) {
        AddContactDialog(
            onDismissRequest = { viewModel.onAddContactDialogDismiss() },
            onConfirm = { peerId -> viewModel.addContact(peerId) }
        )
    }

    // New: Global handler for incoming call
    if (uiState.callState.isRinging) {
        IncomingCallScreen(
            callerId = uiState.callState.peerId,
            onAccept = {
                viewModel.acceptCall()
                // Navigate to the interaction screen for the accepted call
                navController.navigate(Route.Interaction.createRoute(uiState.callState.peerId)) {
                    // Clear back stack to prevent going back to the incoming call screen
                    popUpTo(Screen.Contacts.route)
                }
            },
            onReject = { viewModel.rejectCall() }
        )
    } else {
        NavHost(
            navController = navController,
            startDestination = Screen.Contacts.route,
        ) {
            composable(Screen.Contacts.route) {
                ContactsScreen(
                    viewModel = viewModel,
                    uiState = uiState,
                    onContactClick = { peerId ->
                        viewModel.navigateToPeer(peerId)
                    },
                    onNavigateToProfile = {
                        navController.navigate(Screen.Profile.route)
                    }
                )
            }
            composable(Screen.Profile.route) {
                ProfileScreen(
                    myId = uiState.myId,
                    onBack = { navController.popBackStack() },
                    onNavigateToSettings = { navController.navigate(Screen.Settings.route) }
                )
            }
            composable(Route.Interaction.route) { backStackEntry ->
                val peerId = backStackEntry.arguments?.getString("peerId")
                    ?: return@composable // Or show an error screen
                InteractionScreen(
                    viewModel = viewModel,
                    uiState = uiState,
                    peerId = peerId,
                    onBack = { navController.popBackStack() },
                    onNavigateToCall = {
                        navController.navigate(Route.Call.createRoute(it))
                    }
                )
            }
            composable(Route.Call.route) {
                CallScreen(
                    viewModel = viewModel,
                    uiState = uiState,
                    onNavigateBack = { navController.popBackStack() }
                )
            }
            composable(Screen.Settings.route) {
                SettingsScreen(
                    viewModel = viewModel,
                    onBack = { navController.popBackStack() }
                )
            }
        }
    }
}

// New Composable for the incoming call screen
@Composable
fun IncomingCallScreen(
    callerId: String,
    onAccept: () -> Unit,
    onReject: () -> Unit
) {
    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceAround
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text("Incoming Call", style = MaterialTheme.typography.headlineMedium)
                Spacer(modifier = Modifier.height(16.dp))
                Text(callerId, style = MaterialTheme.typography.titleLarge)
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly
            ) {
                FloatingActionButton(
                    onClick = onReject,
                    containerColor = MaterialTheme.colorScheme.error
                ) {
                    Icon(Icons.Default.CallEnd, contentDescription = "Reject", tint = Color.White)
                }
                FloatingActionButton(
                    onClick = onAccept,
                    containerColor = Color(0xFF4CAF50) // A standard green color for accept
                ) {
                    Icon(Icons.Default.Call, contentDescription = "Accept", tint = Color.White)
                }
            }
        }
    }
}
