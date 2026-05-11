package hela.eillance.viewmodels

import android.app.Application
import android.content.Context
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.events.NativeEventBus
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class NetworkStats(
  val connectedPeers: Int = 0,
  val bootstrapNodesAlive: Int = 0,
  val dhtPeers: Int = 0,
  val deviceRole: String = "simple_node",
  val lastUpdate: Long = 0L
)

class SettingsViewModel(application: Application) : AndroidViewModel(application) {

  private var nativePtr: Long = 0
  private val _connectionState = MutableStateFlow(ServiceConnectionState.Unknown)
  val connectionState = _connectionState.asStateFlow()

  private val _bootstrapNodeUrls = MutableStateFlow("")
  val bootstrapNodeUrls = _bootstrapNodeUrls.asStateFlow()

  private val _peerInactivityTimeout = MutableStateFlow(120)
  val peerInactivityTimeout = _peerInactivityTimeout.asStateFlow()

  private val _customStun = MutableStateFlow("")
  val customStun = _customStun.asStateFlow()

  private val _customTurn = MutableStateFlow("")
  val customTurn = _customTurn.asStateFlow()

  private val _customTurnUser = MutableStateFlow("")
  val customTurnUser = _customTurnUser.asStateFlow()

  private val _customTurnPass = MutableStateFlow("")
  val customTurnPass = _customTurnPass.asStateFlow()

  private val _playSoundOnSend = MutableStateFlow(true)
  val playSoundOnSend = _playSoundOnSend.asStateFlow()

  private val _playSoundOnReceive = MutableStateFlow(true)
  val playSoundOnReceive = _playSoundOnReceive.asStateFlow()

  private val _showNotificationContent = MutableStateFlow(true)
  val showNotificationContent = _showNotificationContent.asStateFlow()

  private val _networkStats = MutableStateFlow(NetworkStats())
  val networkStats = _networkStats.asStateFlow()

  init {
    nativePtr = nativeCreate()
    if (nativePtr == 0L) {
      Log.e("SettingsViewModel", "Failed to create native SettingsViewModel.")
    }
    viewModelScope.launch {
      NativeEventBus.settings.collect { s ->
        if (s != null) {
          _bootstrapNodeUrls.value = s.bootstrapNodeUrls ?: ""
          _peerInactivityTimeout.value = s.timeout
          _customStun.value = s.customStun ?: ""
          _customTurn.value = s.customTurn ?: ""
          _customTurnUser.value = s.customTurnUser ?: ""
          _customTurnPass.value = s.customTurnPass ?: ""
          _playSoundOnSend.value = s.playSoundOnSend
          _playSoundOnReceive.value = s.playSoundOnReceive
          _showNotificationContent.value = s.showNotificationContent
        }
      }
    }
    viewModelScope.launch {
      NativeEventBus.networkStats.collect { stats ->
        _networkStats.value = stats
      }
    }
    viewModelScope.launch {
      NativeEventBus.connectionState.collect { _connectionState.value = it }
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  fun onBootstrapNodeUrlsChange(newUrls: String) {
    _bootstrapNodeUrls.value = newUrls
  }

  fun onPeerInactivityTimeoutChange(newValue: Int) {
    _peerInactivityTimeout.value = newValue
  }

  fun onCustomStunChange(newValue: String) {
    _customStun.value = newValue
  }

  fun onCustomTurnChange(newValue: String) {
    _customTurn.value = newValue
  }

  fun onCustomTurnUserChange(newValue: String) {
    _customTurnUser.value = newValue
  }

  fun onCustomTurnPassChange(newValue: String) {
    _customTurnPass.value = newValue
  }

  fun onShowNotificationContentChange(enabled: Boolean) {
    if (nativePtr != 0L) nativeSetShowNotificationContent(nativePtr, enabled)
  }

  fun onPlaySoundOnSendChange(enabled: Boolean) {
    if (nativePtr != 0L) nativeSetSoundSettings(nativePtr, enabled, _playSoundOnReceive.value)
  }

  fun onPlaySoundOnReceiveChange(enabled: Boolean) {
    if (nativePtr != 0L) nativeSetSoundSettings(nativePtr, _playSoundOnSend.value, enabled)
  }

  fun saveSettings() {
    if (nativePtr != 0L) {
      nativeUpdateBootstrapNodeUrls(nativePtr, _bootstrapNodeUrls.value)
      nativeSetTimeout(nativePtr, _peerInactivityTimeout.value)
      nativeUpdateIceConfig(
        nativePtr,
        _customStun.value,
        _customTurn.value,
        _customTurnUser.value,
        _customTurnPass.value
      )
    }
  }

  private external fun nativeCreate(): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeUpdateBootstrapNodeUrls(ptr: Long, urls: String)
  private external fun nativeSetTimeout(ptr: Long, seconds: Int)
  private external fun nativeUpdateIceConfig(
    ptr: Long,
    stun: String,
    turn: String,
    user: String,
    pass: String
  )

  private external fun nativeSetSoundSettings(ptr: Long, send: Boolean, receive: Boolean)
  private external fun nativeSetShowNotificationContent(ptr: Long, show: Boolean)
}
