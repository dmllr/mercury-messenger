package hela.eillance

import android.app.Application
import android.view.Surface
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.ndkio.AppState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json

class MainViewModel(application: Application) : AndroidViewModel(application) {
    private var callService: CallService? = null

    private val _uiState = MutableStateFlow(AppState())
    val uiState = _uiState.asStateFlow()

    private val _showAddContactDialog = MutableStateFlow(false)
    val showAddContactDialog = _showAddContactDialog.asStateFlow()

    private val json = Json { ignoreUnknownKeys = true }

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

    fun setRemoteVideoSurface(surface: Surface) {
        callService?.initDecoder(surface)
    }

    fun clearRemoteVideoSurface() {
        callService?.releaseDecoder()
    }

    fun loadOlderMessages() {
        callService?.loadOlderMessages()
    }

    fun MarkMessagesAsRead(messageIds: List<Long>) {
        callService?.markMessagesAsRead(messageIds.toLongArray())
    }

    override fun onCleared() {
        callService?.setJsonStateCallback(null)
        super.onCleared()
    }
}
