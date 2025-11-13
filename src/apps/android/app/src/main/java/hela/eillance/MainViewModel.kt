package hela.eillance

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import hela.eillance.ndkio.AppState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.json.Json
import org.webrtc.VideoSink

class MainViewModel(application: Application) : AndroidViewModel(application) {
  private var callService: CallService? = null

  private val _uiState = MutableStateFlow(AppState())
  val uiState = _uiState.asStateFlow() // Exposes the current state to the UI

  private val _showAddContactDialog = MutableStateFlow(false)
  val showAddContactDialog = _showAddContactDialog.asStateFlow()

  private val _showContactProfileDialog = MutableStateFlow<String?>(null)
  val showContactProfileDialog = _showContactProfileDialog.asStateFlow()

  private val _navigateToPeerId = MutableStateFlow<String?>(null)
  val navigateToPeerId = _navigateToPeerId.asStateFlow()

  private val _identitySetupComplete = MutableStateFlow(false)
  val identitySetupComplete = _identitySetupComplete.asStateFlow()

  private var pendingNavigationPeerId: String? = null
  private var pendingAcceptPeerId: String? = null

  private val _signalingUrl = MutableStateFlow("")
  val signalingUrl = _signalingUrl.asStateFlow()


  private val json = Json { ignoreUnknownKeys = true }

  fun setService(service: CallService?) {
    callService = service
    callService?.setJsonStateCallback { jsonState ->
      val newState = json.decodeFromString<AppState>(jsonState)
      _uiState.value = newState
      if (newState.identityExists) _identitySetupComplete.value = true
      _signalingUrl.value = newState.signalingServerUrl ?: ""
    }
    callService?.refreshState()
    // When the service connects, process any pending navigation request.
    pendingNavigationPeerId?.let { peerIdToNavigate ->
      pendingNavigationPeerId = null // Clear before calling to avoid re-entry
      navigateToPeer(peerIdToNavigate)
    }
    // If an accept call action from a notification arrived before the service was bound, execute it now.
    pendingAcceptPeerId?.let { peerIdToAccept ->
      pendingAcceptPeerId = null
      onCallAcceptedFromNotification(peerIdToAccept)
    }
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
    _identitySetupComplete.value = true
    callService?.refreshState()
  }

  // --- Settings ---
  fun onSignalingUrlChange(newUrl: String) {
    _signalingUrl.value = newUrl
  }

  fun saveSettings() {
    callService?.updateSignalingUrl(_signalingUrl.value)
  }

  // --- UI Event Handlers ---

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

  fun addContact(peerId: String) {
    callService?.addContact(peerId)
    onAddContactDialogDismiss()
  }

  fun removeContact(peerId: String) {
    callService?.removeContact(peerId)
    if (_showContactProfileDialog.value == peerId) {
      _showContactProfileDialog.value = null
    }
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
      // 1. Accept the call via the service. This will update the state.
      acceptCall()
      // 2. Navigate to the conversation screen, which will then auto-navigate to the call screen.
      navigateToPeer(peerId)
    } else {
      // Defer the action until the service is connected.
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
      // Always select the contact before navigating to ensure the
      // backend state is correct when the UI destination is reached.
      selectContact(peerId)
      _navigateToPeerId.value = peerId
    }
  }

  fun onNavigationHandled() {
    _navigateToPeerId.value = null
  }

  // --- Profile Management ---

  fun updateMyProfile(name: String, bio: String, photoBase64: String?) {
    callService?.updateMyProfile(name, bio, photoBase64)
  }

  fun setContactDisplayNameOverride(peerId: String, name: String) {
    callService?.setContactDisplayNameOverride(peerId, name)
  }

  fun resetContactDisplayNameOverride(peerId: String) {
    callService?.resetContactDisplayNameOverride(peerId)
  }

  // --- Debug Methods ---
  fun debugEraseAllHistory() {
    callService?.debugEraseAllHistory()
  }
  fun debugEraseAllContacts() {
    callService?.debugEraseAllContacts()
  }
  fun debugRevokeAllApprovals() {
    callService?.debugRevokeAllApprovals()
  }

  override fun onCleared() {
    callService?.setJsonStateCallback(null)
    super.onCleared()
  }
}
