package hela.eillance

import android.net.Uri
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import hela.eillance.navigation.Route
import hela.eillance.navigation.Screen
import hela.eillance.ndkio.Contact
import hela.eillance.screens.BackupAndRestoreScreen
import hela.eillance.screens.CallScreen
import hela.eillance.screens.ContactProfileDialog
import hela.eillance.screens.ContactsScreen
import hela.eillance.screens.ConversationScreen
import hela.eillance.screens.IdentityScreen
import hela.eillance.screens.IncomingCallScreen
import hela.eillance.screens.MnemonicDisplayScreen
import hela.eillance.screens.ProfileScreen
import hela.eillance.screens.RecoverIdentityScreen
import hela.eillance.screens.RecoveryPhraseScreen
import hela.eillance.screens.SettingsScreen
import hela.eillance.screens.WebViewScreen

@Composable
fun MercuryApp(viewModel: MainViewModel) {
  val uiState by viewModel.uiState.collectAsState()

  // The source of truth for navigation is the AppState from the C++ core.
  // If an identity exists, we show the main app. Otherwise, the identity setup flow.
  if (uiState.identityExists) {
    MainAppContent(viewModel = viewModel)
  } else {
    IdentitySetupFlow(viewModel = viewModel)
  }
}

@Composable
fun IdentitySetupFlow(viewModel: MainViewModel) {
  val navController = rememberNavController()
  NavHost(navController = navController, startDestination = Screen.Identity.route) {
    composable(Screen.Identity.route) { IdentityScreen(navController) }
    composable(Screen.MnemonicDisplay.route) { MnemonicDisplayScreen(viewModel) }
    composable(Screen.RecoverIdentity.route) { RecoverIdentityScreen(navController, viewModel) }
    composable(
      route = Route.WebView.route,
      arguments = listOf(navArgument("url") { type = NavType.StringType })
    ) { backStackEntry ->
      backStackEntry.arguments?.getString("url")?.let {
        WebViewScreen(navController = navController, url = Uri.decode(it))
      }
    }
  }
}

@Composable
fun MainAppContent(viewModel: MainViewModel) {
  val navController = rememberNavController()
  val uiState by viewModel.uiState.collectAsState()
  val navigateToPeerId by viewModel.navigateToPeerId.collectAsState()
  val peerIdForProfileDialog by viewModel.showContactProfileDialog.collectAsState()
  var contactToRemove by remember { mutableStateOf<Contact?>(null) }

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

  contactToRemove?.let { contact ->
    AlertDialog(
      onDismissRequest = { contactToRemove = null },
      title = { Text("Remove Contact?") },
      text = { Text("Are you sure you want to remove ${contact.name}? All conversation history will be permanently deleted.") },
      confirmButton = {
        TextButton(
          onClick = {
            // If the user is on a conversation screen when they delete a
            // contact, navigate back to the contacts list to avoid showing
            // an empty screen for the now-deleted contact.
            val isOnConversationScreen = navController.currentBackStackEntry?.destination?.route == Route.Interaction.route
            viewModel.removeContact(contact.peerId)
            contactToRemove = null
            if (isOnConversationScreen) {
              navController.popBackStack()
            }
          },
          colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error)
        ) {
          Text("Remove")
        }
      },
      dismissButton = {
        TextButton(onClick = { contactToRemove = null }) {
          Text("Cancel")
        }
      }
    )
  }

  peerIdForProfileDialog?.let { peerId ->
    val contact = uiState.contacts.find { it.peerId == peerId }
    contact?.let {
      ContactProfileDialog(contact = it,
                           onDismissRequest = { viewModel.onContactProfileDialogDismiss() },
                           onOverrideSave = { newName -> viewModel.setContactDisplayNameOverride(peerId, newName) },
                           onOverrideReset = { viewModel.resetContactDisplayNameOverride(peerId) },
                           onRemoveRequest = { contactToRemove = it })
    }
  }

  // Global handler for incoming call
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
          },
          onShowContactProfile = viewModel::onShowContactProfile
        )
      }
      composable(Screen.Profile.route) {
        ProfileScreen(
          viewModel = viewModel,
          uiState = uiState,
          onBack = { navController.popBackStack() },
          onNavigateToSettings = { navController.navigate(Screen.Settings.route) },
          onNavigateToBackupAndRestore = { navController.navigate(Screen.BackupRestore.route) }
        )
      }
      composable(Route.Interaction.route) { backStackEntry ->
        val peerId = backStackEntry.arguments?.getString("peerId")
          ?: return@composable // Or show an error screen
        ConversationScreen(
          viewModel = viewModel,
          uiState = uiState,
          peerId = peerId,
          onBack = { navController.popBackStack() },
          onNavigateToCall = { navController.navigate(Route.Call.createRoute(it)) },
          onShowContactProfile = viewModel::onShowContactProfile
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
          onBack = { navController.popBackStack() },
          navController = navController
        )
      }
      composable(Screen.BackupRestore.route) {
        BackupAndRestoreScreen(
          viewModel = viewModel,
          onBack = { navController.popBackStack() },
          onNavigateToRecoveryPhrase = { navController.navigate(Screen.RecoveryPhrase.route) }
        )
      }
      composable(Screen.RecoveryPhrase.route) {
        RecoveryPhraseScreen(
          viewModel = viewModel,
          onBack = { navController.popBackStack() }
        )
      }
      composable(
        route = Route.WebView.route,
        arguments = listOf(navArgument("url") { type = NavType.StringType })
      ) { backStackEntry ->
        val url = backStackEntry.arguments?.getString("url")
        if (url != null) {
          WebViewScreen(navController = navController, url = url)
        }
      }
      composable(
        route = Route.WebView.route,
        arguments = listOf(navArgument("url") { type = NavType.StringType })
      ) { backStackEntry ->
        backStackEntry.arguments?.getString("url")?.let {
          WebViewScreen(navController = navController, url = Uri.decode(it))
        }
      }
    }
  }
}
