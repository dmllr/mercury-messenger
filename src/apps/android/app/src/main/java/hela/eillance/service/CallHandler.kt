package hela.eillance.service

import android.content.Context
import android.content.Intent
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.telecom.Connection
import android.util.Log
import hela.eillance.CallSoundPlayer
import hela.eillance.MessengerActivity
import hela.eillance.MessengerService
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.telecom.MercuryConnection
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.webrtc.WebRtcMediaTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import org.webrtc.VideoSink

class CallHandler(
  private val service: MessengerService,
  private val notifications: MercuryNotifications,
  private val serviceScope: CoroutineScope
) {
  private val audioManager = service.getSystemService(Context.AUDIO_SERVICE) as AudioManager
  private val soundPlayer = CallSoundPlayer(service)
  private val telecomIntegration = TelecomIntegration(service)

  var activeConnection: MercuryConnection? = null
  var mediaTransport: WebRtcMediaTransport? = null
  var callViewModel: CallViewModel? = null

  private var lastKnownCallState: CallState? = null

  fun init() {
    audioManager.mode = AudioManager.MODE_NORMAL
  }

  fun onConnectionCreated(connection: MercuryConnection) {
    activeConnection = connection
  }

  fun handleCallStateChange(callState: CallState) {
    // 1. Detect New Incoming Call
    val wasRinging = lastKnownCallState?.isRinging == true
    if (callState.isRinging && callState.callDirection == CallDirection.Incoming && !wasRinging) {
      Log.i(TAG, "Incoming call detected from ${callState.peerId}")
      try {
        telecomIntegration.processIncomingCall(callState.peerId)
      } catch (e: Exception) {
        Log.e(TAG, "Telecom failed, falling back to notification", e)
        notifications.showIncomingCallNotification(callState.peerId)
      }
    }

    // 2. Audio Mode Logic
    if (callState.isRinging && callState.callDirection == CallDirection.Incoming) {
      if (audioManager.mode != AudioManager.MODE_NORMAL) {
        audioManager.mode = AudioManager.MODE_NORMAL
      }
    } else if (callState.isInCall) {
      if (audioManager.mode != AudioManager.MODE_IN_COMMUNICATION) {
        audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
      }
    }

    soundPlayer.updateCallState(callState)

    // 3. Media Logic
    val wasInCall = lastKnownCallState?.isInCall == true
    val isInCall = callState.isInCall

    if (!wasInCall && isInCall) {
      val isVideo = callState.callType == HistoryEntryKind.VideoCall
      mediaTransport?.setAudioEnabled(true)
      if (isVideo) mediaTransport?.setVideoEnabled(true)
    } else if (!isInCall) {
      mediaTransport?.setAudioEnabled(false)
      mediaTransport?.setVideoEnabled(false)
    }

    val wasActive =
      lastKnownCallState?.let { it.isConnecting || it.isRinging || it.isInCall } ?: false
    val isActive = callState.isConnecting || callState.isRinging || callState.isInCall

    if (wasActive && !isActive) {
      stopLocalMedia()
      audioManager.mode = AudioManager.MODE_NORMAL
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        audioManager.clearCommunicationDevice()
      } else {
        @Suppress("DEPRECATION")
        audioManager.isSpeakerphoneOn = false
      }
    }

    syncTelecomState(callState)
    lastKnownCallState = callState
  }

  private fun syncTelecomState(callState: CallState) {
    val isActiveCall = callState.isConnecting || callState.isRinging || callState.isInCall

    if (!isActiveCall && activeConnection != null) {
      Log.i(TAG, "Sync: C++ idle, disconnecting Telecom")
      activeConnection?.setDisconnected(android.telecom.DisconnectCause(android.telecom.DisconnectCause.REMOTE))
      activeConnection?.destroy()
      activeConnection = null
    } else if (callState.isInCall && (activeConnection?.state == Connection.STATE_RINGING || activeConnection?.state == Connection.STATE_DIALING)) {
      Log.i(TAG, "Sync: C++ connected, setting Telecom active")
      activeConnection?.setActive()
      activeConnection?.audioModeIsVoip = true
    }
  }

  fun acceptCall() {
    serviceScope.launch {
      soundPlayer.stop()
      notifications.cancelIncomingCallNotification()

      activeConnection?.let {
        if (it.state == Connection.STATE_RINGING || it.state == Connection.STATE_INITIALIZING) {
          it.setActive()
          it.audioModeIsVoip = true
        }
      }

      callViewModel?.nativeAcceptCallBridge()

      val isVideo = lastKnownCallState?.callType == HistoryEntryKind.VideoCall
      startLocalMedia(isVideo)

      val peerId = lastKnownCallState?.peerId
      // Ensure we don't double-open activity if IncomingCallActivity is already visible on lock screen
      if (peerId != null && !service.isIncomingUIVisible) {
        try {
          // Launch Main Activity to show call screen (CallScreen handles in-call UI)
          service.startActivity(Intent(service, MessengerActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP
            putExtra(MessengerService.EXTRA_PEER_ID, peerId)
          })
        } catch (e: Exception) {
          Log.e(TAG, "Failed to start MessengerActivity from acceptCall", e)
        }
      }
    }
  }

  fun rejectCall() {
    serviceScope.launch {
      Log.i(TAG, "rejectCall requested")
      soundPlayer.stop()
      notifications.cancelIncomingCallNotification()

      activeConnection?.let {
        if (it.state != Connection.STATE_DISCONNECTED) {
          it.setDisconnected(android.telecom.DisconnectCause(android.telecom.DisconnectCause.REJECTED))
          it.destroy()
        }
      }
      activeConnection = null
      callViewModel?.nativeRejectCallBridge()
    }
  }

  fun hangup() {
    serviceScope.launch {
      Log.i(TAG, "hangup requested")
      soundPlayer.stop()

      activeConnection?.let {
        if (it.state != Connection.STATE_DISCONNECTED) {
          it.setDisconnected(android.telecom.DisconnectCause(android.telecom.DisconnectCause.LOCAL))
          it.destroy()
        }
      }
      activeConnection = null
      callViewModel?.nativeHangupBridge()
    }
  }

  fun onTelecomDisconnect(connection: MercuryConnection) {
    serviceScope.launch {
      if (activeConnection == connection) {
        activeConnection = null
        soundPlayer.stop()
        val currentState = lastKnownCallState
        if (currentState != null && (currentState.isRinging || currentState.isInCall || currentState.isConnecting)) {
          if (currentState.isRinging) {
            callViewModel?.nativeRejectCallBridge()
          } else {
            callViewModel?.nativeHangupBridge()
          }
        }
      }
    }
  }

  fun startLocalMedia(isVideo: Boolean) {
    mediaTransport?.startLocalMedia(isVideo)
  }

  fun stopLocalMedia() = mediaTransport?.stopLocalMedia()
  fun setLocalVideoSink(sink: VideoSink?) = mediaTransport?.setLocalSink(sink)
  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) =
    mediaTransport?.setRemoteSink(peerId, sink)

  fun setMicrophoneMute(muted: Boolean) {
    mediaTransport?.setAudioEnabled(!muted)
  }

  fun setSpeakerphoneOn(on: Boolean) {
    val isInCall = lastKnownCallState?.isInCall == true
    if (!isInCall) return

    audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      val devices = audioManager.availableCommunicationDevices
      val targetType =
        if (on) AudioDeviceInfo.TYPE_BUILTIN_SPEAKER else AudioDeviceInfo.TYPE_BUILTIN_EARPIECE
      val device = devices.find { it.type == targetType }
      if (device != null) {
        audioManager.setCommunicationDevice(device)
      } else {
        audioManager.clearCommunicationDevice()
      }
    } else {
      @Suppress("DEPRECATION")
      audioManager.isSpeakerphoneOn = on
    }
  }

  fun stop() {
    soundPlayer.stop()
  }

  fun getLastCallState(): CallState? = lastKnownCallState

  companion object {
    private const val TAG = "CallHandler"
  }
}
