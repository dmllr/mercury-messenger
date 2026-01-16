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
  val myProfile by viewModel.myProfile.collectAsState()
  val isIdentityReady by viewModel.isIdentityReady.collectAsState()
  val showBetaNotice by viewModel.showBetaNotice.collectAsState()
  val lifecycleOwner = LocalLifecycleOwner.current

  // Check permissions on Resume to handle restart/settings changes
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
  val hasCallPermissions by viewModel.hasCallPermissions.collectAsState()
  val hasNotificationPermission by viewModel.hasNotificationPermission.collectAsState()
  val hasBatteryExemption by viewModel.hasBatteryExemption.collectAsState()
  val incomingCallUiVisible by viewModel.incomingCallUiVisible.collectAsState()

  var contactToRemove by remember { mutableStateOf<Contact?>(null) }
  var contactToBlock by remember { mutableStateOf<Contact?>(null) }

  // Global navigation to Call Screen when a call is active (Outgoing or InCall)
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
      title = { Text("Delete Contact?") },
      text = { Text("Are you sure you want to permanently delete ${contact.name}? All conversation history will be lost. This cannot be undone.") },
      confirmButton = {
        TextButton(
          onClick = {
            val isOnConversationScreen = navController.currentBackStackEntry?.destination?.route == Route.Interaction.route
            viewModel.deleteContact(contact.peerId)
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
            viewModel.blockContact(contact.peerId)
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
        onDismissRequest = { viewModel.onContactProfileDialogDismiss() },
        onOverrideSave = { newName -> viewModel.setContactDisplayNameOverride(peerId, newName) },
        onOverrideReset = { viewModel.resetContactDisplayNameOverride(peerId) },
        onRemoveRequest = { contactToRemove = it },
        onBlockRequest = { contactToBlock = it })
    }
  }

  // Only show full screen incoming call UI if ringing AND explicitly requested
  if (callState.isRinging && callState.callDirection == CallDirection.Incoming && incomingCallUiVisible) {
    val caller = contacts.find { it.peerId == callState.peerId }
    IncomingCallScreen(
      callerId = callState.peerId,
      callerName = caller?.name,
      callerPhoto = caller?.photo,
      onAccept = {
        viewModel.acceptCall()
      },
      onReject = { viewModel.rejectCall() }
    )
  } else {
    NavHost(
      navController = navController,
      startDestination = Screen.Contacts.route,
      modifier = Modifier.fillMaxSize()
    ) {
      composable(Screen.Contacts.route) {
        ContactsScreen(
          viewModel = viewModel,
          myProfile = myProfile,
          contacts = contacts,
          connectionState = connectionState,
          hasCallPermissions = hasCallPermissions,
          hasNotificationPermission = hasNotificationPermission,
          hasBatteryExemption = hasBatteryExemption,
          onCheckPermissions = viewModel::checkAllPermissions,
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
          hasCallPermissions = hasCallPermissions,
          hasNotificationPermission = hasNotificationPermission,
          onCheckPermissions = viewModel::checkAllPermissions,
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
          onQrCodeScanned = { peerId, token ->
            viewModel.addContact(peerId, token)
            navController.popBackStack()
          },
          onClose = { navController.popBackStack() }
        )
      }
      composable(Screen.RejectedContacts.route) {
        RejectedContactsScreen(
          viewModel = viewModel,
          onBack = { navController.popBackStack() }
        )
      }
    }
  }
}
