package hela.eillance.viewmodels

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.MessengerService
import hela.eillance.events.NativeEventBus
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.HistoryEntryKind
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.webrtc.VideoSink

class CallViewModel(application: Application) : AndroidViewModel(application) {

  private var nativePtr: Long = 0
  private val _callState = MutableStateFlow(CallState())
  val callState = _callState.asStateFlow()

  private val _isMuted = MutableStateFlow(false)
  val isMuted = _isMuted.asStateFlow()

  private val _isSpeakerOn = MutableStateFlow(false)
  val isSpeakerOn = _isSpeakerOn.asStateFlow()

  // We need direct access to CallService for WebRTC sinks and Transport interactions
  private val messengerService: MessengerService?
    get() = MessengerService.instance

  init {
    nativePtr = nativeCreate()
    if (nativePtr == 0L) {
      Log.e("CallViewModel", "Failed to create native CallViewModel.")
    }
    viewModelScope.launch {
      NativeEventBus.callState.collect { _callState.value = it }
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  fun startVoiceCall(peerId: String? = null) {
    val target = peerId ?: _callState.value.peerId
    if (target.isNotEmpty() && nativePtr != 0L) {
      messengerService?.startVoiceCallViaTelecom(target)
      messengerService?.startLocalMedia(false)
      nativeStartVoice(nativePtr, target)
    }
  }

  fun startVideoCall(peerId: String? = null) {
    val target = peerId ?: _callState.value.peerId
    if (target.isNotEmpty() && nativePtr != 0L) {
      messengerService?.startVideoCallViaTelecom(target)
      messengerService?.startLocalMedia(true)
      nativeStartVideo(nativePtr, target)
    }
  }

  fun hangup() {
    // Delegate to Service for proper cleanup (Telecom, Sound, Notification)
    messengerService?.hangup() ?: run {
      // Fallback if service not bound (unlikely in active call)
      if (nativePtr != 0L) nativeHangup(nativePtr)
    }
  }

  fun acceptCall() {
    // Delegate to Service
    messengerService?.acceptCall() ?: run {
      if (nativePtr != 0L) {
        val isVideo = _callState.value.callType == HistoryEntryKind.VideoCall
        // Fallback
        nativeAccept(nativePtr)
      }
    }
  }

  fun rejectCall() {
    // Delegate to Service to stop ringing and kill notification
    messengerService?.rejectCall() ?: run {
      if (nativePtr != 0L) nativeReject(nativePtr)
    }
  }

  fun stopCallMedia() {
    _isMuted.value = false
    _isSpeakerOn.value = false
    messengerService?.stopLocalMedia()
    messengerService?.setSpeakerphoneOn(false)
  }

  fun setLocalVideoSink(sink: VideoSink?) {
    messengerService?.setLocalVideoSink(sink)
  }

  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) {
    messengerService?.setRemoteVideoSink(peerId, sink)
  }

  fun toggleMute() {
    val newMuted = !_isMuted.value
    _isMuted.value = newMuted
    messengerService?.setMicrophoneMute(newMuted)
  }

  fun toggleSpeaker() {
    val newSpeaker = !_isSpeakerOn.value
    _isSpeakerOn.value = newSpeaker
    messengerService?.setSpeakerphoneOn(newSpeaker)
  }

  // Exposed for CallService to call into C++
  fun nativeAcceptCallBridge() {
    if (nativePtr != 0L) nativeAccept(nativePtr)
  }

  fun nativeRejectCallBridge() {
    if (nativePtr != 0L) nativeReject(nativePtr)
  }

  fun nativeHangupBridge() {
    if (nativePtr != 0L) nativeHangup(nativePtr)
  }

  private external fun nativeCreate(): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeStartVoice(ptr: Long, peerId: String)
  private external fun nativeStartVideo(ptr: Long, peerId: String)
  private external fun nativeHangup(ptr: Long)
  private external fun nativeAccept(ptr: Long)
  private external fun nativeReject(ptr: Long)
}
