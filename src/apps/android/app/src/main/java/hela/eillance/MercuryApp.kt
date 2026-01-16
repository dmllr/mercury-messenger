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
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import hela.eillance.navigation.Route
import hela.eillance.navigation.Screen
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.Contact
import hela.eillance.screens.BackupAndRestoreScreen
import hela.eillance.screens.BetaNoticeDialog
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
import hela.eillance.screens.RejectedContactsScreen
import hela.eillance.screens.SettingsScreen
import hela.eillance.screens.WebViewScreen

@Composable
fun MercuryApp(viewModel: MainViewModel) {
  val myProfile by viewModel.profileViewModel.myProfile.collectAsState()
  val isIdentityReady by viewModel.isIdentityReady.collectAsState()
  val showBetaNotice by viewModel.showBetaNotice.collectAsState()
  val lifecycleOwner = LocalLifecycleOwner.current

  DisposableEffect(lifecycleOwner) {
    val observer = LifecycleEventObserver { _, event ->
      if (event == Lifecycle.Event.ON_RESUME) {
        viewModel.checkAllPermissions()
      }
    }
    lifecycleOwner.lifecycle.addObserver(observer)
    onDispose {
      lifecycleOwner.lifecycle.removeObserver(observer)
    }
  }

  if (showBetaNotice) {
    BetaNoticeDialog(onDismiss = viewModel::dismissBetaNotice)
  }

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
    composable(Screen.MnemonicDisplay.route) { MnemonicDisplayScreen(viewModel.profileViewModel, viewModel::finishIdentitySetup) }
    composable(Screen.RecoverIdentity.route) { RecoverIdentityScreen(navController, viewModel.profileViewModel) }
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

  val contactsVM = viewModel.contactsViewModel
  val chatVM = viewModel.conversationViewModel
  val callVM = viewModel.callViewModel
  val profileVM = viewModel.profileViewModel
  val settingsVM = viewModel.settingsViewModel

  val myProfile by profileVM.myProfile.collectAsState()
  val contacts by contactsVM.contacts.collectAsState()
  val callState by callVM.callState.collectAsState()
  val connectionState by settingsVM.connectionState.collectAsState()

  val navigateToPeerId by viewModel.navigateToPeerId.collectAsState()
  val peerIdForProfileDialog by contactsVM.showContactProfileDialog.collectAsState()
  val navigateToScanner by viewModel.navigateToScanner.collectAsState()
  val incomingCallUiVisible by viewModel.incomingCallUiVisible.collectAsState()

  // Permissions state
  val hasCallPermissions by viewModel.hasCallPermissions.collectAsState()
  val hasNotificationPermission by viewModel.hasNotificationPermission.collectAsState()
  val hasBatteryExemption by viewModel.hasBatteryExemption.collectAsState()

  var contactToRemove by remember { mutableStateOf<Contact?>(null) }
  var contactToBlock by remember { mutableStateOf<Contact?>(null) }

  // Global Navigation: Call
  LaunchedEffect(callState) {
    if (callState.isInCall || callState.isConnecting || (callState.isRinging && callState.callDirection == CallDirection.Outgoing)) {
      val route = Route.Call.createRoute(callState.peerId)
      val currentRoute = navController.currentDestination?.route
      if (currentRoute != Route.Call.route && currentRoute?.startsWith(Route.Call.route.substringBefore("{")) != true) {
        navController.navigate(route) {
          launchSingleTop = true
        }
      }
    }
  }

  // Global Navigation: Chat
  LaunchedEffect(navigateToPeerId) {
    navigateToPeerId?.let { peerId ->
      if (!callState.isConnecting && !callState.isInCall && !callState.isRinging) {
        navController.navigate(Route.Interaction.createRoute(peerId)) {
          launchSingleTop = true
        }
      }
      viewModel.onNavigationHandled()
    }
  }

  // Global Navigation: Scanner
  LaunchedEffect(navigateToScanner) {
    if (navigateToScanner) {
      navController.navigate(Screen.QrScanner.route)
      viewModel.onScannerNavigated()
    }
  }

  if (contactsVM.showAddContactDialog.collectAsState().value) {
    AddContactDialog(
      onDismissRequest = { contactsVM.onAddContactDialogDismiss() },
      onConfirm = { peerId -> contactsVM.addContact(peerId) },
      onScanQrCode = { viewModel.onScanQrClicked(); contactsVM.onAddContactDialogDismiss() }
    )
  }

  contactToRemove?.let { contact ->
    AlertDialog(
      onDismissRequest = { contactToRemove = null },
      title = { Text("Delete Contact?") },
      text = { Text("Are you sure you want to permanently delete ${contact.name}? All conversation history will be lost. This cannot be undone.") },
      confirmButton = {
        TextButton(
          onClick = {
            val isOnConversationScreen = navController.currentBackStackEntry?.destination?.route == Route.Interaction.route
            contactsVM.deleteContact(contact.peerId)
            contactToRemove = null
            if (isOnConversationScreen) {
              navController.popBackStack()
            }
          },
          colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error)
        ) {
          Text("Delete")
        }
      },
      dismissButton = {
        TextButton(onClick = { contactToRemove = null }) {
          Text("Cancel")
        }
      }
    )
  }

  contactToBlock?.let { contact ->
    AlertDialog(
      onDismissRequest = { contactToBlock = null },
      title = { Text("Block Contact?") },
      text = { Text("Are you sure you want to block ${contact.name}? You will no longer receive messages or calls from them.") },
      confirmButton = {
        TextButton(
          onClick = {
            val isOnConversationScreen = navController.currentBackStackEntry?.destination?.route == Route.Interaction.route
            contactsVM.blockContact(contact.peerId)
            contactToBlock = null
            if (isOnConversationScreen) {
              navController.popBackStack()
            }
          },
          colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error)
        ) {
          Text("Block")
        }
      },
      dismissButton = {
        TextButton(onClick = { contactToBlock = null }) {
          Text("Cancel")
        }
      }
    )
  }

  peerIdForProfileDialog?.let { peerId ->
    contacts.find { it.peerId == peerId }?.let {
      ContactProfileDialog(contact = it,
        onDismissRequest = { contactsVM.onContactProfileDialogDismiss() },
        onOverrideSave = { newName -> contactsVM.setContactDisplayNameOverride(peerId, newName) },
        onOverrideReset = { contactsVM.resetContactDisplayNameOverride(peerId) },
        onRemoveRequest = { contactToRemove = it },
        onBlockRequest = { contactToBlock = it })
    }
  }

  if (callState.isRinging && callState.callDirection == CallDirection.Incoming && incomingCallUiVisible) {
    val caller = contacts.find { it.peerId == callState.peerId }
    IncomingCallScreen(
      callerId = callState.peerId,
      callerName = caller?.name,
      callerPhoto = caller?.photo,
      onAccept = { callVM.acceptCall() },
      onReject = { callVM.rejectCall() }
    )
  } else {
    NavHost(
      navController = navController,
      startDestination = Screen.Contacts.route,
      modifier = Modifier.fillMaxSize()
    ) {
      composable(Screen.Contacts.route) {
        ContactsScreen(
          contactsVM = contactsVM,
          myProfile = myProfile,
          contacts = contacts,
          connectionState = connectionState,
          hasCallPermissions = hasCallPermissions,
          hasNotificationPermission = hasNotificationPermission,
          hasBatteryExemption = hasBatteryExemption,
          onCheckPermissions = viewModel::checkAllPermissions,
          onContactClick = { peerId -> viewModel.navigateToPeer(peerId) },
          onNavigateToProfile = { navController.navigate(Screen.Profile.route) },
          onShowContactProfile = contactsVM::onShowContactProfile
        )
      }
      composable(Screen.Profile.route) {
        ProfileScreen(
          profileVM = profileVM,
          contactsVM = contactsVM,
          conversationVM = chatVM,
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
        val historyPage by chatVM.interactionHistoryPage.collectAsState()

        ConversationScreen(
          chatVM = chatVM,
          contactsVM = contactsVM,
          callVM = callVM,
          contact = contact,
          historyPage = historyPage,
          callState = callState,
          peerId = peerId,
          hasCallPermissions = hasCallPermissions,
          hasNotificationPermission = hasNotificationPermission,
          onCheckPermissions = viewModel::checkAllPermissions,
          onBack = { navController.popBackStack() },
          onNavigateToCall = { navController.navigate(Route.Call.createRoute(it)) },
          onShowContactProfile = contactsVM::onShowContactProfile
        )
      }
      composable(Route.Call.route) {
        CallScreen(
          callVM = callVM,
          callState = callState,
          onNavigateBack = { navController.popBackStack() }
        )
      }
      composable(Screen.Settings.route) {
        SettingsScreen(
          settingsVM = settingsVM,
          onBack = { navController.popBackStack() },
          navController = navController
        )
      }
      composable(Screen.BackupRestore.route) {
        BackupAndRestoreScreen(
          profileVM = profileVM,
          onBack = { navController.popBackStack() },
          onNavigateToRecoveryPhrase = { navController.navigate(Screen.RecoveryPhrase.route) }
        )
      }
      composable(Screen.RecoveryPhrase.route) {
        RecoveryPhraseScreen(
          profileVM = profileVM,
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
          onQrCodeScanned = { peerId, token ->
            contactsVM.addContact(peerId, token)
            navController.popBackStack()
          },
          onClose = { navController.popBackStack() }
        )
      }
      composable(Screen.RejectedContacts.route) {
        RejectedContactsScreen(
          contactsVM = contactsVM,
          onBack = { navController.popBackStack() }
        )
      }
    }
  }
}
