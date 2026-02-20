package hela.eillance.viewmodels

import android.app.Application
import android.content.Context
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class SettingsViewModel(application: Application) : AndroidViewModel(application),
  NativeSettingsObserver {

  private var nativePtr: Long = 0
  private val _connectionState = MutableStateFlow(ServiceConnectionState.Unknown)
  val connectionState = _connectionState.asStateFlow()

  private val _signalingUrl = MutableStateFlow("")
  val signalingUrl = _signalingUrl.asStateFlow()

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

  private val prefs = application.getSharedPreferences("mercury_prefs", Context.MODE_PRIVATE)
  private val _showNotificationContent =
    MutableStateFlow(prefs.getBoolean("show_notifications_content", true))
  val showNotificationContent = _showNotificationContent.asStateFlow()

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

  override fun onSettingsUpdated(
    signalingUrl: String?,
    timeout: Int,
    customStun: String?,
    customTurn: String?,
    customTurnUser: String?,
    customTurnPass: String?,
    playSoundOnSend: Boolean,
    playSoundOnReceive: Boolean
  ) {
    _signalingUrl.value = signalingUrl ?: ""
    _peerInactivityTimeout.value = timeout
    _customStun.value = customStun ?: ""
    _customTurn.value = customTurn ?: ""
    _customTurnUser.value = customTurnUser ?: ""
    _customTurnPass.value = customTurnPass ?: ""
    _playSoundOnSend.value = playSoundOnSend
    _playSoundOnReceive.value = playSoundOnReceive
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
    _showNotificationContent.value = enabled
    prefs.edit().putBoolean("show_notifications_content", enabled).apply()
  }

  fun onPlaySoundOnSendChange(enabled: Boolean) {
    if (nativePtr != 0L) nativeSetSoundSettings(nativePtr, enabled, _playSoundOnReceive.value)
  }

  fun onPlaySoundOnReceiveChange(enabled: Boolean) {
    if (nativePtr != 0L) nativeSetSoundSettings(nativePtr, _playSoundOnSend.value, enabled)
  }

  fun saveSettings() {
    if (nativePtr != 0L) {
      nativeUpdateUrl(nativePtr, _signalingUrl.value)
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

  private external fun nativeCreate(observer: NativeSettingsObserver): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeUpdateUrl(ptr: Long, url: String)
  private external fun nativeSetTimeout(ptr: Long, seconds: Int)
  private external fun nativeUpdateIceConfig(
    ptr: Long,
    stun: String,
    turn: String,
    user: String,
    pass: String
  )

  private external fun nativeSetSoundSettings(ptr: Long, send: Boolean, receive: Boolean)
}
