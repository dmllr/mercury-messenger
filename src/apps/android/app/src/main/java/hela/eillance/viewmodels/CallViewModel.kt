package hela.eillance.viewmodels

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.MessengerService
import hela.eillance.events.NativeEventBus
import hela.eillance.ndkio.CallState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.webrtc.VideoSink

class CallViewModel(application: Application) : AndroidViewModel(application) {

  private var nativePtr: Long = 0
  private val _callState = MutableStateFlow(CallState())
  val callState = _callState.asStateFlow()

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
      nativeStartVoice(nativePtr, target)
    }
  }

  fun startVideoCall(peerId: String? = null) {
    val target = peerId ?: _callState.value.peerId
    if (target.isNotEmpty() && nativePtr != 0L) {
      nativeStartVideo(nativePtr, target)
    }
  }

  fun hangup() {
    // If user presses hangup, we delegate to Service. Service will call into our native bridge.
    messengerService?.hangup() ?: run {
      if (nativePtr != 0L) nativeHangup(nativePtr)
    }
  }

  fun acceptCall() {
    messengerService?.acceptCall() ?: run {
      if (nativePtr != 0L) {
        nativeAccept(nativePtr)
      }
    }
  }

  fun rejectCall() {
    messengerService?.rejectCall() ?: run {
      if (nativePtr != 0L) nativeReject(nativePtr)
    }
  }

  fun stopCallMedia() {
    if (nativePtr != 0L) nativeStopCallMedia(nativePtr)
  }

  fun setLocalVideoSink(sink: VideoSink?) {
    messengerService?.setLocalVideoSink(sink)
  }

  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) {
    messengerService?.setRemoteVideoSink(peerId, sink)
  }

  fun toggleMute() {
    if (nativePtr != 0L) nativeToggleMute(nativePtr)
  }

  fun toggleSpeaker() {
    if (nativePtr != 0L) nativeToggleSpeaker(nativePtr)
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
  private external fun nativeToggleMute(ptr: Long)
  private external fun nativeToggleSpeaker(ptr: Long)
  private external fun nativeStopCallMedia(ptr: Long)
}
