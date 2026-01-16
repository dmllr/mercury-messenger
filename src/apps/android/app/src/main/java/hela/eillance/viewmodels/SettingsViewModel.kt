package hela.eillance.viewmodels

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import hela.eillance.CallService
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class SettingsViewModel(application: Application) : AndroidViewModel(application) {
  private var callService: CallService? = null

  private val _connectionState = MutableStateFlow(ServiceConnectionState.Unknown)
  val connectionState = _connectionState.asStateFlow()

  private val _signalingUrl = MutableStateFlow("")
  val signalingUrl = _signalingUrl.asStateFlow()

  private val _peerInactivityTimeout = MutableStateFlow(120)
  val peerInactivityTimeout = _peerInactivityTimeout.asStateFlow()

  fun setService(service: CallService?) {
    callService = service
  }

  fun refreshConnectionState() {
    _connectionState.value = ServiceConnectionState.fromInt(callService?.nativeGetConnectionState() ?: 0)
  }

  fun refreshSignalingUrl() {
    _signalingUrl.value = callService?.nativeGetSignalingUrl() ?: ""
  }

  fun refreshPeerInactivityTimeout() {
    _peerInactivityTimeout.value = callService?.nativeGetPeerInactivityTimeout() ?: 120
  }

  fun onSignalingUrlChange(newUrl: String) {
    _signalingUrl.value = newUrl
  }

  fun onPeerInactivityTimeoutChange(newValue: Int) {
    _peerInactivityTimeout.value = newValue
  }

  fun saveSettings() {
    callService?.updateSignalingUrl(_signalingUrl.value)
    callService?.updatePeerInactivityTimeout(_peerInactivityTimeout.value)
  }
}
