package hela.eillance.viewmodels

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import hela.eillance.CallService
import hela.eillance.ndkio.CallState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.webrtc.VideoSink

class CallViewModel(application: Application) : AndroidViewModel(application) {
  private var callService: CallService? = null

  private val _callState = MutableStateFlow(CallState())
  val callState = _callState.asStateFlow()

  fun setService(service: CallService?) {
    callService = service
  }

  fun refreshCallState() {
    _callState.value = callService?.nativeGetCallState() ?: CallState()
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

  fun setLocalVideoSink(sink: VideoSink?) {
    callService?.setLocalVideoSink(sink)
  }

  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) {
    callService?.setRemoteVideoSink(peerId, sink)
  }
}
