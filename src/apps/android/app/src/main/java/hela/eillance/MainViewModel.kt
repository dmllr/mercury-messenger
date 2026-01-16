package hela.eillance

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.ndkio.StateChange
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.ConversationViewModel
import hela.eillance.viewmodels.ProfileViewModel
import hela.eillance.viewmodels.SettingsViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class MainViewModel(application: Application) : AndroidViewModel(application) {
  private var callService: CallService? = null

  // Sub-ViewModels
  val contactsViewModel = ContactsViewModel(application)
  val conversationViewModel = ConversationViewModel(application)
  val callViewModel = CallViewModel(application)
  val profileViewModel = ProfileViewModel(application)
  val settingsViewModel = SettingsViewModel(application)

  // Global App State (Navigation, Permissions, System)
  private val _isIdentityReady = MutableStateFlow(false)
  val isIdentityReady = _isIdentityReady.asStateFlow()

  private val _showBetaNotice = MutableStateFlow(false)
  val showBetaNotice = _showBetaNotice.asStateFlow()

  private val _hasCallPermissions = MutableStateFlow(false)
  val hasCallPermissions = _hasCallPermissions.asStateFlow()

  private val _hasNotificationPermission = MutableStateFlow(true)
  val hasNotificationPermission = _hasNotificationPermission.asStateFlow()

  private val _hasBatteryExemption = MutableStateFlow(true)
  val hasBatteryExemption = _hasBatteryExemption.asStateFlow()

  private val _incomingCallUiVisible = MutableStateFlow(false)
  val incomingCallUiVisible = _incomingCallUiVisible.asStateFlow()

  private val _navigateToPeerId = MutableStateFlow<String?>(null)
  val navigateToPeerId = _navigateToPeerId.asStateFlow()

  private val _navigateToScanner = MutableStateFlow(false)
  val navigateToScanner = _navigateToScanner.asStateFlow()

  // Coordination variables
  private var pendingNavigationPeerId: String? = null
  private var pendingAcceptPeerId: String? = null

  init {
    checkBetaNotice()
    checkAllPermissions()
  }

  // --- Service Binding & State Routing ---

  fun setService(service: CallService?) {
    callService = service

    // Propagate service to sub-models
    contactsViewModel.setService(service)
    conversationViewModel.setService(service)
    callViewModel.setService(service)
    profileViewModel.setService(service)
    settingsViewModel.setService(service)

    service?.setStateChangedCallback { changeSet ->
      handleStateChanges(changeSet)
    }

    service?.refreshState()

    // Handle pending intents
    pendingNavigationPeerId?.let {
      navigateToPeer(it)
      pendingNavigationPeerId = null
    }
    pendingAcceptPeerId?.let {
      onCallAcceptedFromNotification(it)
      pendingAcceptPeerId = null
    }
  }

  private fun handleStateChanges(changeSet: IntArray) {
    viewModelScope.launch(Dispatchers.IO) {
      val changes = changeSet.map { StateChange.fromInt(it) }.toSet()

      // Route changes to appropriate Sub-VMs
      if (changes.contains(StateChange.MyProfile)) {
        profileViewModel.refreshMyProfile()
        _isIdentityReady.value = true
      }
      if (changes.contains(StateChange.Contacts)) contactsViewModel.refreshContacts()
      if (changes.contains(StateChange.Connection)) settingsViewModel.refreshConnectionState()
      if (changes.contains(StateChange.SelectedContact)) conversationViewModel.refreshSelectedContact()
      if (changes.contains(StateChange.InteractionHistory)) conversationViewModel.refreshHistory()
      if (changes.contains(StateChange.CallState)) {
        callViewModel.refreshCallState()
        // Orchestration: Hide incoming UI if ringing stops
        if (!callViewModel.callState.value.isRinging) {
          _incomingCallUiVisible.value = false
        }
      }
      if (changes.contains(StateChange.SignalingUrl)) settingsViewModel.refreshSignalingUrl()
      if (changes.contains(StateChange.PeerInactivityTimeout)) settingsViewModel.refreshPeerInactivityTimeout()

      callService?.handleNotifications()
    }
  }

  // --- Global Navigation & Permissions ---

  private fun checkBetaNotice() {
    val prefs = getApplication<Application>().getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
    val hasSeenNotice = prefs.getBoolean("has_seen_beta_notice_v1", false)
    _showBetaNotice.value = !hasSeenNotice
  }

  fun dismissBetaNotice() {
    val prefs = getApplication<Application>().getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
    prefs.edit().putBoolean("has_seen_beta_notice_v1", true).apply()
    _showBetaNotice.value = false
  }

  fun checkAllPermissions() {
    val pm = PermissionManager(getApplication())
    _hasCallPermissions.value = pm.hasCallPermissions()
    _hasNotificationPermission.value = pm.hasNotificationPermission()
    _hasBatteryExemption.value = pm.hasBatteryExemption()
  }

  fun finishIdentitySetup() {
    _isIdentityReady.value = true
    callService?.refreshState()
  }

  fun showIncomingCallUi() {
    _incomingCallUiVisible.value = true
  }

  fun onScanQrClicked() {
    _navigateToScanner.value = true
  }

  fun onScannerNavigated() {
    _navigateToScanner.value = false
  }

  fun navigateToPeer(peerId: String) {
    if (callService == null) {
      pendingNavigationPeerId = peerId
    } else {
      conversationViewModel.selectContact(peerId)
      _navigateToPeerId.value = peerId
    }
  }

  fun onNavigationHandled() {
    _navigateToPeerId.value = null
  }

  fun onCallAcceptedFromNotification(peerId: String) {
    if (callService != null) {
      callViewModel.acceptCall()
      navigateToPeer(peerId)
    } else {
      pendingAcceptPeerId = peerId
    }
  }

  // Debug routing
  fun debugEraseAllHistory() = conversationViewModel.debugEraseAllHistory()
  fun debugEraseAllContacts() = contactsViewModel.debugEraseAllContacts()
  fun debugRevokeAllApprovals() = contactsViewModel.debugRevokeAllApprovals()

  override fun onCleared() {
    callService?.setStateChangedCallback(null)
    super.onCleared()
  }
}
