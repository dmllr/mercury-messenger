package hela.eillance

import android.net.Uri
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
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
import hela.eillance.screens.HelpScreen
import hela.eillance.screens.IdentityScreen
import hela.eillance.screens.IncomingCallScreen
import hela.eillance.screens.MnemonicDisplayScreen
import hela.eillance.screens.ProfileScreen
import hela.eillance.screens.QrScannerScreen
import hela.eillance.screens.RecoverIdentityScreen
import hela.eillance.screens.RecoveryPhraseScreen
import hela.eillance.screens.SettingsScreen
import hela.eillance.screens.WebViewScreen

@Composable
fun MercuryApp(viewModel: MainViewModel) {
  val myProfile by viewModel.myProfile.collectAsState()
  val isIdentityReady by viewModel.isIdentityReady.collectAsState()

  if (!isIdentityReady) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
      CircularProgressIndicator()
    }
  } else {
    if (myProfile.identityExists) {
      MainAppContent(viewModel = viewModel)
    } else {
      IdentitySetupFlow(viewModel = viewModel)
    }
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

  // Collect all necessary states from the ViewModel
  val myProfile by viewModel.myProfile.collectAsState()
  val contacts by viewModel.contacts.collectAsState()
  val historyPage by viewModel.interactionHistoryPage.collectAsState()
  val callState by viewModel.callState.collectAsState()
  val connectionState by viewModel.connectionState.collectAsState()
  val navigateToPeerId by viewModel.navigateToPeerId.collectAsState()
  val peerIdForProfileDialog by viewModel.showContactProfileDialog.collectAsState()
  val navigateToScanner by viewModel.navigateToScanner.collectAsState()

  var contactToRemove by remember { mutableStateOf<Contact?>(null) }

  LaunchedEffect(navigateToPeerId) {
    navigateToPeerId?.let { peerId ->
      if (!callState.isInCall && !callState.isRinging) {
        navController.navigate(Route.Interaction.createRoute(peerId)) {
          launchSingleTop = true
        }
      }
      viewModel.onNavigationHandled()
    }
  }

  LaunchedEffect(navigateToScanner) {
    if (navigateToScanner) {
      navController.navigate(Screen.QrScanner.route)
      viewModel.onScannerNavigated()
    }
  }

  if (viewModel.showAddContactDialog.collectAsState().value) {
    AddContactDialog(
      onDismissRequest = { viewModel.onAddContactDialogDismiss() },
      onConfirm = { peerId -> viewModel.addContact(peerId) },
      onScanQrCode = { viewModel.onScanQrClicked() }
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
    contacts.find { it.peerId == peerId }?.let {
      ContactProfileDialog(contact = it,
        onDismissRequest = { viewModel.onContactProfileDialogDismiss() },
        onOverrideSave = { newName -> viewModel.setContactDisplayNameOverride(peerId, newName) },
        onOverrideReset = { viewModel.resetContactDisplayNameOverride(peerId) },
        onRemoveRequest = { contactToRemove = it })
    }
  }

  if (callState.isRinging) {
    IncomingCallScreen(
      callerId = callState.peerId,
      onAccept = {
        viewModel.acceptCall()
        navController.navigate(Route.Interaction.createRoute(callState.peerId)) {
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
          myProfile = myProfile,
          contacts = contacts,
          connectionState = connectionState,
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
          navController = navController,
          viewModel = viewModel,
          myProfile = myProfile,
          onBack = { navController.popBackStack() },
          onNavigateToSettings = { navController.navigate(Screen.Settings.route) },
          onNavigateToBackupAndRestore = { navController.navigate(Screen.BackupRestore.route) },
          onNavigateToHelp = { navController.navigate(Screen.Help.route) }
        )
      }
      composable(Route.Interaction.route) { backStackEntry ->
        val peerId = backStackEntry.arguments?.getString("peerId")
          ?: return@composable
        val contact = contacts.find { it.peerId == peerId }
        ConversationScreen(
          viewModel = viewModel,
          contact = contact,
          historyPage = historyPage,
          callState = callState,
          peerId = peerId,
          onBack = { navController.popBackStack() },
          onNavigateToCall = { navController.navigate(Route.Call.createRoute(it)) },
          onShowContactProfile = viewModel::onShowContactProfile
        )
      }
      composable(Route.Call.route) {
        CallScreen(
          viewModel = viewModel,
          callState = callState,
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
      composable(Screen.Help.route) {
        HelpScreen(navController = navController)
      }
      composable(Screen.QrScanner.route) {
        QrScannerScreen(
          onQrCodeScanned = { result ->
            viewModel.addContact(result)
            navController.popBackStack()
          },
          onClose = { navController.popBackStack() }
        )
      }
    }
  }
}
