package hela.eillance.viewmodels

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import hela.eillance.CallService
import hela.eillance.ndkio.CallState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.webrtc.VideoSink

class CallViewModel(application: Application) : AndroidViewModel(application), NativeCallObserver {

  private var nativePtr: Long = 0
  private val _callState = MutableStateFlow(CallState())
  val callState = _callState.asStateFlow()

  // We need direct access to CallService for WebRTC sinks and Transport interactions
  // which are still bound to the Service context for now.
  private val callService: CallService?
    get() = CallService.instance

  init {
    nativePtr = nativeCreate(this)
    if (nativePtr == 0L) {
      Log.e("CallViewModel", "Failed to create native CallViewModel.")
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  override fun onCallStateChanged(state: CallState) {
    _callState.value = state
  }

  fun startVoiceCall(peerId: String? = null) {
    val target = peerId ?: _callState.value.peerId
    if (target.isNotEmpty() && nativePtr != 0L) {
      // Trigger Telecom in CallService (Android specific), then call native
      callService?.startVoiceCallViaTelecom(target)
      nativeStartVoice(nativePtr, target)
    }
  }

  fun startVideoCall(peerId: String? = null) {
    val target = peerId ?: _callState.value.peerId
    if (target.isNotEmpty() && nativePtr != 0L) {
      callService?.startVideoCallViaTelecom(target)
      nativeStartVideo(nativePtr, target)
    }
  }

  fun hangup() {
    if (nativePtr != 0L) nativeHangup(nativePtr)
  }

  fun acceptCall() {
    if (nativePtr != 0L) nativeAccept(nativePtr)
  }

  fun rejectCall() {
    if (nativePtr != 0L) nativeReject(nativePtr)
  }

  fun startCallMedia() {
    callService?.startLocalMedia()
  }

  fun stopCallMedia() {
    callService?.stopLocalMedia()
  }

  fun setLocalVideoSink(sink: VideoSink?) {
    callService?.setLocalVideoSink(sink)
  }

  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) {
    callService?.setRemoteVideoSink(peerId, sink)
  }

  private external fun nativeCreate(observer: NativeCallObserver): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeStartVoice(ptr: Long, peerId: String)
  private external fun nativeStartVideo(ptr: Long, peerId: String)
  private external fun nativeHangup(ptr: Long)
  private external fun nativeAccept(ptr: Long)
  private external fun nativeReject(ptr: Long)
}
