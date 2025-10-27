package hela.eillance

import android.app.Application
import android.content.Context
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.ndkio.AppState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json
import org.webrtc.VideoSink

class MainViewModel(application: Application) : AndroidViewModel(application) {
    private var callService: CallService? = null
    private val sharedPrefs = application.getSharedPreferences("mercury_settings", Context.MODE_PRIVATE)

    private val _uiState = MutableStateFlow(AppState())
    val uiState = _uiState.asStateFlow()

    private val _showAddContactDialog = MutableStateFlow(false)
    val showAddContactDialog = _showAddContactDialog.asStateFlow()

    private val _navigateToPeerId = MutableStateFlow<String?>(null)
    val navigateToPeerId = _navigateToPeerId.asStateFlow()

    private var pendingNavigationPeerId: String? = null

    private val _signalingUrl = MutableStateFlow("")
    val signalingUrl = _signalingUrl.asStateFlow()


    private val json = Json { ignoreUnknownKeys = true }

    init {
        loadSettings()
    }

    fun setService(service: CallService?) {
        callService = service
        callService?.setJsonStateCallback { jsonState ->
            viewModelScope.launch {
                try {
                    val newState = json.decodeFromString<AppState>(jsonState)
                    _uiState.update { newState }
                } catch (e: Exception) {
                    Log.e("MainViewModel", "Failed to decode AppState from JSON", e)
                }
            }
        }
        callService?.refreshState()
        // When the service connects, process any pending navigation request.
        pendingNavigationPeerId?.let { peerIdToNavigate ->
            pendingNavigationPeerId = null // Clear before calling to avoid re-entry
            navigateToPeer(peerIdToNavigate)
        }
    }

    // --- Settings ---
    private fun loadSettings() {
        _signalingUrl.value = sharedPrefs.getString("signaling_server_url", "") ?: ""
    }

    fun onSignalingUrlChange(newUrl: String) {
        _signalingUrl.value = newUrl
    }

    fun saveSettings() {
        viewModelScope.launch {
            val newUrl = _signalingUrl.value
            with(sharedPrefs.edit()) {
                putString("signaling_server_url", newUrl)
                apply()
            }
            callService?.reconnectSignaling(newUrl)
        }
    }


    // --- UI Event Handlers ---

    fun onAddContactClicked() {
        _showAddContactDialog.value = true
    }

    fun onAddContactDialogDismiss() {
        _showAddContactDialog.value = false
    }

    fun addContact(peerId: String) {
        callService?.addContact(peerId)
        onAddContactDialogDismiss()
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

    override fun onCleared() {
        callService?.setJsonStateCallback(null)
        super.onCleared()
    }
}
