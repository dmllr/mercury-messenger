package hela.eillance

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.ndkio.StateChange
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.webrtc.VideoSink

class MainViewModel(application: Application) : AndroidViewModel(application) {
  private var callService: CallService? = null

  private val _myProfile = MutableStateFlow(MyProfileInfo(identityExists = false))
  val myProfile = _myProfile.asStateFlow()

  private val _contacts = MutableStateFlow<List<Contact>>(emptyList())
  val contacts = _contacts.asStateFlow()

  private val _interactionHistoryPage = MutableStateFlow(HistoryPage())
  val interactionHistoryPage = _interactionHistoryPage.asStateFlow()

  private val _callState = MutableStateFlow(CallState())
  val callState = _callState.asStateFlow()

  private val _connectionState = MutableStateFlow(ServiceConnectionState.Unknown)
  val connectionState = _connectionState.asStateFlow()

  private val _selectedContactId = MutableStateFlow<String?>(null)
  val selectedContactId = _selectedContactId.asStateFlow()

  private val _signalingUrl = MutableStateFlow("")
  val signalingUrl = _signalingUrl.asStateFlow()

  private val _showAddContactDialog = MutableStateFlow(false)
  val showAddContactDialog = _showAddContactDialog.asStateFlow()

  private val _showContactProfileDialog = MutableStateFlow<String?>(null)
  val showContactProfileDialog = _showContactProfileDialog.asStateFlow()

  private val _navigateToPeerId = MutableStateFlow<String?>(null)
  val navigateToPeerId = _navigateToPeerId.asStateFlow()

  private val _navigateToScanner = MutableStateFlow(false)
  val navigateToScanner = _navigateToScanner.asStateFlow()

  private val _isIdentityReady = MutableStateFlow(false)
  val isIdentityReady = _isIdentityReady.asStateFlow()

  private var pendingNavigationPeerId: String? = null
  private var pendingAcceptPeerId: String? = null

  fun setService(service: CallService?) {
    callService = service
    callService?.setStateChangedCallback { changeSet ->
      handleStateChanges(changeSet)
    }
    callService?.refreshState()

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
      if (changes.contains(StateChange.MyProfile)) refreshMyProfile()
      if (changes.contains(StateChange.Contacts)) refreshContacts()
      if (changes.contains(StateChange.Connection)) refreshConnectionState()
      if (changes.contains(StateChange.SelectedContact)) refreshSelectedContact()
      if (changes.contains(StateChange.InteractionHistory)) refreshHistory()
      if (changes.contains(StateChange.CallState)) refreshCallState()
      if (changes.contains(StateChange.SignalingUrl)) refreshSignalingUrl()

      callService?.handleNotifications()
    }
  }

  private fun refreshMyProfile() {
    callService?.nativeGetMyProfile()?.let {
      _myProfile.value = it
      _isIdentityReady.value = true
    }
  }

  private fun refreshContacts() {
    _contacts.value = callService?.nativeGetContacts() ?: emptyList()
  }

  private fun refreshConnectionState() {
    _connectionState.value = ServiceConnectionState.fromInt(callService?.nativeGetConnectionState() ?: 0)
  }

  private fun refreshSelectedContact() {
    _selectedContactId.value = callService?.nativeGetSelectedContactId()
  }

  private fun refreshHistory() {
    _interactionHistoryPage.value = callService?.nativeGetInteractionHistoryPage() ?: HistoryPage()
  }

  private fun refreshCallState() {
    _callState.value = callService?.nativeGetCallState() ?: CallState()
  }

  private fun refreshSignalingUrl() {
    _signalingUrl.value = callService?.nativeGetSignalingUrl() ?: ""
  }

  fun generateNewIdentity(): String {
    return callService?.generateNewIdentity() ?: ""
  }

  fun restoreIdentity(mnemonic: String): Boolean {
    return callService?.restoreIdentity(mnemonic) ?: false
  }

  fun getMnemonic(): String? {
    return callService?.getMnemonic()
  }

  fun createBackup(path: String, password: String): Boolean {
    return callService?.createBackup(path, password) ?: false
  }

  fun restoreBackup(uri: Uri, password: String): Boolean {
    return callService?.restoreBackup(uri, password) ?: false
  }

  fun finishIdentitySetup() {
    _isIdentityReady.value = true
    callService?.refreshState()
  }

  fun onSignalingUrlChange(newUrl: String) {
    _signalingUrl.value = newUrl
  }

  fun saveSettings() {
    callService?.updateSignalingUrl(_signalingUrl.value)
  }

  fun onAddContactClicked() {
    _showAddContactDialog.value = true
  }

  fun onShowContactProfile(peerId: String) {
    _showContactProfileDialog.value = peerId
  }

  fun onContactProfileDialogDismiss() {
    _showContactProfileDialog.value = null
  }

  fun onAddContactDialogDismiss() {
    _showAddContactDialog.value = false
  }

  fun onScanQrClicked() {
    _showAddContactDialog.value = false
    _navigateToScanner.value = true
  }

  fun onScannerNavigated() {
    _navigateToScanner.value = false
  }

  fun addContact(peerId: String, token: String? = null) {
    callService?.addContact(peerId, token)
    onAddContactDialogDismiss()
  }

  fun generateContactToken(): String? {
    return callService?.generateContactToken()
  }

  fun blockContact(peerId: String) {
    callService?.blockContact(peerId)
    if (_showContactProfileDialog.value == peerId) {
      _showContactProfileDialog.value = null
    }
  }

  fun deleteContact(peerId: String) {
    callService?.deleteContact(peerId)
  }

  fun unblockContact(peerId: String) {
    callService?.unblockContact(peerId)
  }

  fun acceptContact(peerId: String) {
    callService?.acceptContact(peerId)
  }

  fun selectContact(peerId: String) {
    callService?.selectContact(peerId)
  }

  fun sendMessage(message: String) {
    if (message.isNotBlank()) {
      callService?.sendMessage(message)
    }
  }

  fun startVoiceCall() {
    callService?.startVoiceCall()
  }

  fun startVideoCall() {
    callService?.startVideoCall()
  }

  fun hangup() {
    callService?.hangup()
  }

  fun onCallAcceptedFromNotification(peerId: String) {
    if (callService != null) {
      acceptCall()
      navigateToPeer(peerId)
    } else {
      pendingAcceptPeerId = peerId
    }
  }

  fun acceptCall() {
    callService?.acceptCall()
  }

  fun rejectCall() {
    callService?.rejectCall()
  }

  fun startCallMedia() {
    callService?.startLocalMedia()
  }

  fun stopCallMedia() {
    callService?.stopLocalMedia()
  }

  fun loadOlderMessages() {
    callService?.loadOlderMessages()
  }

  fun loadFullMessage(messageId: Long) {
    callService?.loadFullMessage(messageId)
  }

  fun MarkMessagesAsRead(messageIds: List<Long>) {
    callService?.markMessagesAsRead(messageIds.toLongArray())
  }

  fun setLocalVideoSink(sink: VideoSink?) {
    callService?.setLocalVideoSink(sink)
  }

  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) {
    callService?.setRemoteVideoSink(peerId, sink)
  }

  fun navigateToPeer(peerId: String) {
    if (callService == null) {
      pendingNavigationPeerId = peerId
    } else {
      selectContact(peerId)
      _navigateToPeerId.value = peerId
    }
  }

  fun onNavigationHandled() {
    _navigateToPeerId.value = null
  }

  fun updateMyProfile(name: String, bio: String, photoBase64: String?) {
    callService?.updateMyProfile(name, bio, photoBase64)
  }

  fun setContactDisplayNameOverride(peerId: String, name: String) {
    callService?.setContactDisplayNameOverride(peerId, name)
  }

  fun resetContactDisplayNameOverride(peerId: String) {
    callService?.resetContactDisplayNameOverride(peerId)
  }

  fun debugEraseAllHistory() = callService?.debugEraseAllHistory()
  fun debugEraseAllContacts() = callService?.debugEraseAllContacts()
  fun debugRevokeAllApprovals() = callService?.debugRevokeAllApprovals()

  override fun onCleared() {
    callService?.setStateChangedCallback(null)
    super.onCleared()
  }
}
