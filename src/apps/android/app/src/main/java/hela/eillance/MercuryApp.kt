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
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import hela.eillance.navigation.Route
import hela.eillance.navigation.Screen
import hela.eillance.ndkio.AttachmentType
import hela.eillance.ndkio.CallDirection
import hela.eillance.screens.BackupAndRestoreScreen
import hela.eillance.screens.BetaNoticeDialog
import hela.eillance.screens.CallScreen
import hela.eillance.screens.ContactProfileDialog
import hela.eillance.screens.ContactsScreen
import hela.eillance.screens.ConversationScreen
import hela.eillance.screens.FullScreenMediaScreen
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
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.ConversationViewModel
import hela.eillance.viewmodels.DebugViewModel
import hela.eillance.viewmodels.ProfileViewModel
import hela.eillance.viewmodels.SettingsViewModel

@Composable
fun MercuryApp(viewModel: MainViewModel) {
  val showBetaNotice by viewModel.showBetaNotice.collectAsState()
  val lifecycleOwner = LocalLifecycleOwner.current

  // Wait for the service to initialize the engine.
  // We observe the static StateFlow from CallService companion to avoid issues with instance nullability
  // during early app startup.
  val isEngineReady by CallService.isEngineReady.collectAsState()

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

  if (!isEngineReady) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
      CircularProgressIndicator()
    }
  } else {
    // Only create ViewModels after engine is ready
    val profileVM: ProfileViewModel = viewModel()
    val myProfile by profileVM.myProfile.collectAsState()

    if (myProfile.identityExists) {
      MainAppContent(viewModel = viewModel)
    } else {
      IdentitySetupFlow(profileVM)
    }
  }
}

@Composable
fun IdentitySetupFlow(profileVM: ProfileViewModel) {
  val navController = rememberNavController()
  NavHost(navController = navController, startDestination = Screen.Identity.route) {
    composable(Screen.Identity.route) { IdentityScreen(navController) }
    composable(Screen.MnemonicDisplay.route) { MnemonicDisplayScreen(profileVM) {
      // Upon finishing setup, profileVM state updates will trigger navigation in MercuryApp
    }}
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
fun MainAppContent(viewModel: MainViewModel) {
  val navController = rememberNavController()

  val callVM: CallViewModel = viewModel()
  val callState by callVM.callState.collectAsState()

  val navigateToPeerId by viewModel.navigateToPeerId.collectAsState()
  val navigateToScanner by viewModel.navigateToScanner.collectAsState()
  val incomingCallUiVisible by viewModel.incomingCallUiVisible.collectAsState()

  val hasCallPermissions by viewModel.hasCallPermissions.collectAsState()
  val hasNotificationPermission by viewModel.hasNotificationPermission.collectAsState()
  val hasBatteryExemption by viewModel.hasBatteryExemption.collectAsState()

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
    // Orchestrate incoming call UI visibility
    if (!callState.isRinging) {
      viewModel.hideIncomingCallUi()
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

  if (callState.isRinging && callState.callDirection == CallDirection.Incoming && incomingCallUiVisible) {
    IncomingCallScreen(
      callerId = callState.peerId,
      callerName = null, // TODO: Fetch name/photo
      callerPhoto = null,
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
        val contactsVM: ContactsViewModel = viewModel()
        val profileVM: ProfileViewModel = viewModel()
        val settingsVM: SettingsViewModel = viewModel()

        val contacts by contactsVM.contacts.collectAsState()
        val myProfile by profileVM.myProfile.collectAsState()
        val connectionState by settingsVM.connectionState.collectAsState()
        val showAddDialog by contactsVM.showAddContactDialog.collectAsState()
        val profileDialogPeerId by contactsVM.showContactProfileDialog.collectAsState()

        if (showAddDialog) {
          AddContactDialog(
            onDismissRequest = { contactsVM.onAddContactDialogDismiss() },
            onConfirm = { peerId -> contactsVM.addContact(peerId) },
            onScanQrCode = { viewModel.onScanQrClicked(); contactsVM.onAddContactDialogDismiss() }
          )
        }

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
        val profileVM: ProfileViewModel = viewModel()
        val contactsVM: ContactsViewModel = viewModel()
        val debugVM: DebugViewModel = viewModel()
        val myProfile by profileVM.myProfile.collectAsState()

        ProfileScreen(
          profileVM = profileVM,
          contactsVM = contactsVM,
          debugVM = debugVM,
          myProfile = myProfile,
          onBack = { navController.popBackStack() },
          onNavigateToSettings = { navController.navigate(Screen.Settings.route) },
          onNavigateToBackupAndRestore = { navController.navigate(Screen.BackupRestore.route) },
          onNavigateToHelp = { navController.navigate(Screen.Help.route) }
        )
      }

      composable(Route.Interaction.route) { backStackEntry ->
        val peerId = backStackEntry.arguments?.getString("peerId") ?: return@composable
        val app = LocalContext.current.applicationContext as Application
        val chatVM: ConversationViewModel = viewModel(factory = ConversationViewModel.Factory(app, peerId))
        val contactsVM: ContactsViewModel = viewModel()
        val historyPage by chatVM.interactionHistoryPage.collectAsState()
        val contacts by contactsVM.contacts.collectAsState()
        val contact = contacts.find { it.peerId == peerId }

        ConversationScreen(
          chatVM = chatVM,
          contactsVM = contactsVM,
          callVM = callVM,
          peerId = peerId,
          contact = contact,
          historyPage = historyPage,
          callState = callState,
          hasCallPermissions = hasCallPermissions,
          hasNotificationPermission = hasNotificationPermission,
          onCheckPermissions = viewModel::checkAllPermissions,
          onBack = { navController.popBackStack() },
          onNavigateToCall = { navController.navigate(Route.Call.createRoute(it)) },
          onShowContactProfile = { contactsVM.onShowContactProfile(it) },
          onMediaClick = { att ->
            navController.navigate(Route.FullScreenMedia.createRoute(att.path, att.type.value))
          }
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
        val settingsVM: SettingsViewModel = viewModel()
        SettingsScreen(
          settingsVM = settingsVM,
          onBack = { navController.popBackStack() },
          navController = navController
        )
      }

      composable(Screen.BackupRestore.route) {
        val profileVM: ProfileViewModel = viewModel()
        BackupAndRestoreScreen(
          profileVM = profileVM,
          onBack = { navController.popBackStack() },
          onNavigateToRecoveryPhrase = { navController.navigate(Screen.RecoveryPhrase.route) }
        )
      }

      composable(Screen.RecoveryPhrase.route) {
        val profileVM: ProfileViewModel = viewModel()
        RecoveryPhraseScreen(profileVM, onBack = { navController.popBackStack() })
      }

      composable(Screen.QrScanner.route) {
        val contactsVM: ContactsViewModel = viewModel()
        QrScannerScreen(
          onQrCodeScanned = { peerId, token ->
            contactsVM.addContact(peerId, token)
            navController.popBackStack()
          },
          onClose = { navController.popBackStack() }
        )
      }

      composable(Screen.RejectedContacts.route) {
        val contactsVM: ContactsViewModel = viewModel()
        RejectedContactsScreen(contactsVM, onBack = { navController.popBackStack() })
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
          navArgument("path") { type = NavType.StringType },
          navArgument("type") { type = NavType.IntType }
        )
      ) { backStackEntry ->
        val path = backStackEntry.arguments?.getString("path")
        val typeVal = backStackEntry.arguments?.getInt("type") ?: AttachmentType.File.value
        if (path != null) {
          val type = AttachmentType.fromInt(typeVal)
          FullScreenMediaScreen(path = Uri.decode(path), type = type, onBack = { navController.popBackStack() })
        }
      }
    }
  }
}
