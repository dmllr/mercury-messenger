package hela.eillance.viewmodels

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class SettingsViewModel(application: Application) : AndroidViewModel(application), NativeSettingsObserver {

  private var nativePtr: Long = 0
  private val _connectionState = MutableStateFlow(ServiceConnectionState.Unknown)
  val connectionState = _connectionState.asStateFlow()

  private val _signalingUrl = MutableStateFlow("")
  val signalingUrl = _signalingUrl.asStateFlow()

  private val _peerInactivityTimeout = MutableStateFlow(120)
  val peerInactivityTimeout = _peerInactivityTimeout.asStateFlow()

  init {
    nativePtr = nativeCreate(this)
    if (nativePtr == 0L) {
      Log.e("SettingsViewModel", "Failed to create native SettingsViewModel.")
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  override fun onSettingsUpdated(signalingUrl: String?, timeout: Int) {
    _signalingUrl.value = signalingUrl ?: ""
    _peerInactivityTimeout.value = timeout
  }

  override fun onConnectionStateChanged(state: Int) {
    _connectionState.value = ServiceConnectionState.fromInt(state)
  }

  fun onSignalingUrlChange(newUrl: String) {
    _signalingUrl.value = newUrl
  }

  fun onPeerInactivityTimeoutChange(newValue: Int) {
    _peerInactivityTimeout.value = newValue
  }

  fun saveSettings() {
    if (nativePtr != 0L) {
      nativeUpdateUrl(nativePtr, _signalingUrl.value)
      nativeSetTimeout(nativePtr, _peerInactivityTimeout.value)
    }
  }

  private external fun nativeCreate(observer: NativeSettingsObserver): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeUpdateUrl(ptr: Long, url: String)
  private external fun nativeSetTimeout(ptr: Long, seconds: Int)
}
