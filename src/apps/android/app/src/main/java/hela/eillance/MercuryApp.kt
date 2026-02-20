package hela.eillance

import android.app.Application
import android.net.Uri
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import hela.eillance.navigation.Route
import hela.eillance.navigation.Screen
import hela.eillance.ndkio.CallDirection
import hela.eillance.screens.BackupAndRestoreScreen
import hela.eillance.screens.CallScreen
import hela.eillance.screens.ConnectionSettingsScreen
import hela.eillance.screens.ContactProfileDialog
import hela.eillance.screens.ContactsScreen
import hela.eillance.screens.ConversationScreen
import hela.eillance.screens.HelpScreen
import hela.eillance.screens.IdentityScreen
import hela.eillance.screens.IncomingCallScreen
import hela.eillance.screens.MediaPreviewScreen
import hela.eillance.screens.MnemonicDisplayScreen
import hela.eillance.screens.PermissionIssuesScreen
import hela.eillance.screens.ProfileScreen
import hela.eillance.screens.QrScannerScreen
import hela.eillance.screens.RecoverIdentityScreen
import hela.eillance.screens.RecoveryPhraseScreen
import hela.eillance.screens.RejectedContactsScreen
import hela.eillance.screens.SettingsScreen
import hela.eillance.screens.WebViewScreen
import hela.eillance.screens.debug.ConnectionDebugScreen
import hela.eillance.screens.debug.DebugMenuScreen
import hela.eillance.screens.debug.PendingActionsScreen
import hela.eillance.screens.debug.UnreadMessagesScreen
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.ConversationViewModel
import hela.eillance.viewmodels.DebugViewModel
import hela.eillance.viewmodels.ProfileViewModel
import hela.eillance.viewmodels.SettingsViewModel

@Composable
fun MercuryApp(viewModel: MainViewModel) {
  val lifecycleOwner = androidx.lifecycle.compose.LocalLifecycleOwner.current

  // Wait for the service to initialize the engine.
  val isEngineReady by MessengerService.isEngineReady.collectAsState()

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

  if (!isEngineReady) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
      CircularProgressIndicator()
    }
  } else {
    // Only create ViewModels after engine is ready
    val profileVM: ProfileViewModel = viewModel()
    val myProfile by profileVM.myProfile.collectAsState()

    if (myProfile.identityExists) {
      MainAppContent(viewModel = viewModel, profileVM = profileVM)
    } else {
      IdentitySetupFlow(profileVM)
    }
  }
}

@Composable
fun IdentitySetupFlow(profileVM: ProfileViewModel) {
  val navController = rememberNavController()
  NavHost(navController = navController, startDestination = Screen.Identity.route) {
    composable(Screen.Identity.route) { IdentityScreen(navController, profileVM) }
    composable(Screen.MnemonicDisplay.route) {
      MnemonicDisplayScreen(profileVM) {
        // Upon finishing setup, profileVM state updates will trigger navigation in MercuryApp
      }
    }
    composable(Screen.RecoverIdentity.route) { RecoverIdentityScreen(navController, profileVM) }
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
fun MainAppContent(viewModel: MainViewModel, profileVM: ProfileViewModel) {
  val navController = rememberNavController()

  // Helper to prevent double-pop blank screen issues
  fun NavController.safePopBackStack() {
    if (currentBackStackEntry?.lifecycle?.currentState?.isAtLeast(Lifecycle.State.STARTED) == true) {
      popBackStack()
    }
  }

  // Hoist ViewModels to share instances across the graph
  val callVM: CallViewModel = viewModel()
  val contactsVM: ContactsViewModel = viewModel()
  val settingsVM: SettingsViewModel = viewModel()
  val debugVM: DebugViewModel = viewModel()

  val callState by callVM.callState.collectAsState()
  val myProfile by profileVM.myProfile.collectAsState()

  val navigateToPeerId by viewModel.navigateToPeerId.collectAsState()
  val navigateToScanner by viewModel.navigateToScanner.collectAsState()
  val navigateToContacts by viewModel.navigateToContacts.collectAsState()
  val isCoreUnresponsive by viewModel.isCoreUnresponsive.collectAsState()

  // -- Global State Observations --
  val contacts by contactsVM.contacts.collectAsState()
  val profileDialogPeerId by contactsVM.showContactProfileDialog.collectAsState()

  // -- Global Dialogs --
  profileDialogPeerId?.let { pid ->
    contacts.find { it.peerId == pid }?.let { c ->
      ContactProfileDialog(
        contact = c,
        onDismissRequest = { contactsVM.onContactProfileDialogDismiss() },
        onOverrideSave = { name -> contactsVM.setContactDisplayNameOverride(pid, name) },
        onOverrideReset = { contactsVM.resetContactDisplayNameOverride(pid) },
        onRemoveRequest = { contactsVM.deleteContact(pid); contactsVM.onContactProfileDialogDismiss() },
        onBlockRequest = { contactsVM.blockContact(pid) }
      )
    }
  }

  // Orchestrate incoming call UI visibility
  LaunchedEffect(callState.isRinging) {
    if (!callState.isRinging) {
      viewModel.hideIncomingCallUi()
    }
  }

  // Global Navigation: Contacts (for Share reset)
  LaunchedEffect(navigateToContacts) {
    if (navigateToContacts) {
      navController.navigate(Screen.Contacts.route) {
        popUpTo(0)
      }
      viewModel.onContactsNavigated()
    }
  }

  // Global Navigation: Chat
  LaunchedEffect(navigateToPeerId) {
    navigateToPeerId?.let { (peerId, msgId) ->
      navController.navigate(Route.Interaction.createRoute(peerId, msgId)) {
        launchSingleTop = true
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

  // Determine Call Status for Prioritized UI
  val isIncomingCall = callState.isRinging && callState.callDirection == CallDirection.Incoming
  val isActiveCall = callState.isConnecting || callState.isInCall ||
      (callState.isRinging && callState.callDirection == CallDirection.Outgoing)

  if (isIncomingCall) {
    val callerContact = contacts.find { it.peerId == callState.peerId }
    val callerName = callerContact?.name
    val callerPhoto = callerContact?.photo

    IncomingCallScreen(
      callerId = callState.peerId,
      callerName = callerName,
      callerPhoto = callerPhoto,
      callType = callState.callType,
      callVM = callVM,
      onAccept = { callVM.acceptCall() },
      onReject = { callVM.rejectCall() }
    )
  } else if (isActiveCall) {
    // Resolve contact info for active call
    val activeContact = contacts.find { it.peerId == callState.peerId }
    val contactName = activeContact?.name
    val contactPhoto = activeContact?.photo

    CallScreen(
      callVM = callVM,
      callState = callState,
      contactName = contactName,
      contactPhoto = contactPhoto
    )
  } else {
    NavHost(
      navController = navController,
      startDestination = Screen.Contacts.route,
      modifier = Modifier.fillMaxSize()
    ) {
      composable(Screen.Contacts.route) {
        val contactsList by contactsVM.contacts.collectAsState()
        val isLoading by contactsVM.isLoading.collectAsState()
        val connectionState by settingsVM.connectionState.collectAsState()
        val showAddDialog by contactsVM.showAddContactDialog.collectAsState()

        if (showAddDialog) {
          AddContactDialog(
            onDismissRequest = { contactsVM.onAddContactDialogDismiss() },
            onConfirm = { peerId -> contactsVM.addContact(peerId) },
            onScanQrCode = { viewModel.onScanQrClicked(); contactsVM.onAddContactDialogDismiss() }
          )
        }

        ContactsScreen(
          contactsVM = contactsVM,
          mainViewModel = viewModel,
          myProfile = myProfile,
          contacts = contactsList,
          isLoading = isLoading,
          connectionState = connectionState,
          isCoreUnresponsive = isCoreUnresponsive,
          isInCall = isActiveCall, // Although covered by the if/else, this supports the snackbar logic
          onResetCore = viewModel::resetCore,
          onCheckPermissions = viewModel::checkAllPermissions,
          onContactClick = { peerId, msgId -> viewModel.navigateToPeer(peerId, msgId) },
          onNavigateToProfile = {
            navController.navigate(Screen.Profile.route) {
              launchSingleTop = true
            }
          },
          onShowContactProfile = contactsVM::onShowContactProfile
        )
      }

      composable(Screen.Profile.route) {
        ProfileScreen(
          profileVM = profileVM,
          contactsVM = contactsVM,
          settingsVM = settingsVM,
          debugVM = debugVM,
          mainViewModel = viewModel,
          myProfile = myProfile,
          onBack = { navController.safePopBackStack() },
          onNavigateToSettings = { navController.navigate(Screen.Settings.route) },
          onNavigateToBetaNotice = { navController.navigate(Route.WebView.createRoute("https://mercury.eillance.com/beta/embed/")) },
          onNavigateToBackupAndRestore = { navController.navigate(Screen.BackupRestore.route) },
          onNavigateToHelp = { navController.navigate(Screen.Help.route) },
          onNavigateToPermissionIssues = { navController.navigate(Screen.PermissionIssues.route) },
          onNavigateToDebugMenu = { navController.navigate(Screen.DebugMenu.route) }
        )
      }

      composable(Screen.DebugMenu.route) {
        DebugMenuScreen(
          onNavigateBack = { navController.safePopBackStack() },
          onNavigateToPendingActions = { navController.navigate(Screen.DebugPendingActions.route) },
          onNavigateToConnections = { navController.navigate(Screen.DebugConnections.route) },
          onNavigateToUnread = { navController.navigate(Screen.DebugUnread.route) },
          onDebugAction = { action ->
            when (action) {
              hela.eillance.screens.debug.DebugAction.ERASE_HISTORY -> debugVM.eraseAllHistory()
              hela.eillance.screens.debug.DebugAction.ERASE_CONTACTS -> debugVM.eraseAllContacts()
              hela.eillance.screens.debug.DebugAction.REVOKE_APPROVALS -> debugVM.revokeAllApprovals()
            }
          }
        )
      }

      composable(Screen.DebugPendingActions.route) {
        PendingActionsScreen(debugVM = debugVM, onBack = { navController.safePopBackStack() })
      }

      composable(Screen.DebugConnections.route) {
        ConnectionDebugScreen(debugVM = debugVM, onBack = { navController.safePopBackStack() })
      }

      composable(Screen.DebugUnread.route) {
        UnreadMessagesScreen(debugVM = debugVM, onBack = { navController.safePopBackStack() })
      }

      composable(Screen.PermissionIssues.route) {
        PermissionIssuesScreen(
          mainViewModel = viewModel,
          onBack = { navController.safePopBackStack() }
        )
      }

      composable(
        route = Route.Interaction.route,
        arguments = listOf(
          navArgument("peerId") { type = NavType.StringType },
          navArgument("messageId") {
            type = NavType.StringType; nullable = true; defaultValue = null
          }
        )
      ) { backStackEntry ->
        val peerId = backStackEntry.arguments?.getString("peerId") ?: return@composable
        val messageIdStr = backStackEntry.arguments?.getString("messageId")
        val messageId = messageIdStr?.toLongOrNull()
        val app = LocalContext.current.applicationContext as Application
        val chatVM: ConversationViewModel =
          viewModel(factory = ConversationViewModel.Factory(app, peerId))
        val historyPage by chatVM.interactionHistoryPage.collectAsState()
        val contactsList by contactsVM.contacts.collectAsState()
        val contact = contactsList.find { it.peerId == peerId }
        val connectionState by settingsVM.connectionState.collectAsState()

        ConversationScreen(
          chatVM = chatVM,
          contactsVM = contactsVM,
          settingsVM = settingsVM,
          callVM = callVM,
          targetMessageId = messageId,
          mainViewModel = viewModel,
          peerId = peerId,
          contact = contact,
          historyPage = historyPage,
          myId = myProfile.myId,
          callState = callState,
          connectionState = connectionState,
          onBack = { navController.safePopBackStack() },
          onNavigateToCall = { /* No-op, handled by global state */ },
          onShowContactProfile = { contactsVM.onShowContactProfile(it) },
          onMediaClick = { attachments, index ->
            navController.navigate(Route.FullScreenMedia.createRoute(peerId, attachments, index))
          }
        )
      }

      composable(Screen.Settings.route) {
        SettingsScreen(
          settingsVM = settingsVM,
          onBack = { navController.safePopBackStack() },
          navController = navController
        )
      }

      composable(Screen.ConnectionSettings.route) {
        ConnectionSettingsScreen(
          settingsVM = settingsVM,
          onBack = { navController.safePopBackStack() }
        )
      }

      composable(Screen.BackupRestore.route) {
        BackupAndRestoreScreen(
          profileVM = profileVM,
          onBack = { navController.safePopBackStack() },
          onNavigateToRecoveryPhrase = { navController.navigate(Screen.RecoveryPhrase.route) },
          onRestoreSuccess = {
            // Restore successful: The C++ engine has reloaded.
            // Navigate to Contacts and clear the back stack to reset UI state.
            navController.navigate(Screen.Contacts.route) {
              popUpTo(0) // Clear entire back stack
            }
          }
        )
      }

      composable(Screen.RecoveryPhrase.route) {
        RecoveryPhraseScreen(profileVM, onBack = { navController.safePopBackStack() })
      }

      composable(Screen.QrScanner.route) {
        QrScannerScreen(
          onQrCodeScanned = { peerId, token ->
            contactsVM.addContact(peerId, token)
            navController.safePopBackStack()
          },
          onClose = { navController.safePopBackStack() }
        )
      }

      composable(Screen.RejectedContacts.route) {
        RejectedContactsScreen(contactsVM, onBack = { navController.safePopBackStack() })
      }

      composable(Screen.Help.route) {
        HelpScreen(navController)
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
        route = Route.FullScreenMedia.route,
        arguments = listOf(
          navArgument("peerId") { type = NavType.StringType },
          navArgument("attachments") { type = NavType.StringType },
          navArgument("index") { type = NavType.IntType; defaultValue = 0 }
        )
      ) { backStackEntry ->
        val peerId = backStackEntry.arguments?.getString("peerId")
        val attachmentsJson = backStackEntry.arguments?.getString("attachments")
        val index = backStackEntry.arguments?.getInt("index") ?: 0

        if (peerId != null && attachmentsJson != null) {
          MediaPreviewScreen(
            peerId = peerId,
            attachmentsJson = Uri.decode(attachmentsJson),
            initialIndex = index,
            onBack = { navController.safePopBackStack() }
          )
        }
      }
    }
  }
}
