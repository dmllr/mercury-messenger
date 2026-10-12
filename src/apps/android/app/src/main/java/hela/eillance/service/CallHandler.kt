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
import hela.eillance.ndkio.CallState
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

  fun init() {
    audioManager.mode = AudioManager.MODE_NORMAL
  }

  fun onConnectionCreated(connection: MercuryConnection) {
    activeConnection = connection
  }

  // --- Core-Driven OS Orchestration ---

  fun processIncomingTelecomCall(peerId: String) {
    try {
      telecomIntegration.processIncomingCall(peerId)
    } catch (e: Exception) {
      Log.e(TAG, "Telecom failed, falling back to notification", e)
      notifications.showIncomingCallNotification(peerId)
    }
  }

  fun processOutgoingTelecomCall(peerId: String, isVideo: Boolean) {
    telecomIntegration.placeTelecomCall(peerId, isVideo)
  }

  fun disconnectTelecomCall(peerId: String) {
    activeConnection?.let {
      if (it.state != Connection.STATE_DISCONNECTED) {
        it.setDisconnected(android.telecom.DisconnectCause(android.telecom.DisconnectCause.LOCAL))
        it.destroy()
      }
    }
    activeConnection = null
  }

  fun setTelecomCallActive(peerId: String) {
    activeConnection?.let {
      it.setActive()
      it.audioModeIsVoip = true
    }
  }

  fun setAudioMode(mode: Int) {
    if (mode == 1) {
      if (audioManager.mode != AudioManager.MODE_IN_COMMUNICATION) {
        audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
      }
    } else {
      if (audioManager.mode != AudioManager.MODE_NORMAL) {
        audioManager.mode = AudioManager.MODE_NORMAL
      }
    }
  }

  fun clearCommunicationDevice() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      audioManager.clearCommunicationDevice()
    } else {
      @Suppress("DEPRECATION")
      audioManager.isSpeakerphoneOn = false
    }
  }

  fun playSound(type: Int) {
    val soundState = when (type) {
      0 -> CallSoundPlayer.SoundState.Ringing
      1 -> CallSoundPlayer.SoundState.Ringback
      2 -> CallSoundPlayer.SoundState.Connecting
      else -> return
    }
    soundPlayer.startSound(soundState)
  }

  fun stopSound() {
    soundPlayer.stop()
    notifications.cancelIncomingCallNotification()
  }

  // --- End Orchestration ---

  fun acceptCall() {
    serviceScope.launch {
      soundPlayer.stop()
      notifications.cancelIncomingCallNotification()
      service.nativeAcceptCall()

      val peerId = getLastCallState()?.peerId
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
      service.nativeRejectCall()
    }
  }

  fun hangup() {
    serviceScope.launch {
      Log.i(TAG, "hangup requested")
      soundPlayer.stop()
      service.nativeHangupCall()
    }
  }

  fun onTelecomDisconnect(connection: MercuryConnection) {
    serviceScope.launch {
      if (activeConnection == connection) {
        activeConnection = null
        soundPlayer.stop()
        service.nativeTelecomCallEnded()
      }
    }
  }

  // Expose these for manual UI overrides (e.g. CallScreen swapping sinks)
  fun setLocalVideoSink(sink: VideoSink?) = mediaTransport?.setLocalSink(sink)
  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) =
    mediaTransport?.setRemoteSink(peerId, sink)

  fun setMicrophoneMute(muted: Boolean) {
    mediaTransport?.setAudioEnabled(!muted)
  }

  fun setSpeakerphoneOn(on: Boolean) {
    val isInCall = getLastCallState()?.isInCall == true
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

  fun stopLocalMedia() {
    mediaTransport?.stopLocalMedia()
  }

  fun stop() {
    soundPlayer.stop()
    notifications.cancelIncomingCallNotification()
  }

  fun getLastCallState(): CallState? = hela.eillance.events.NativeEventBus.callState.value

  companion object {
    private const val TAG = "CallHandler"
  }
}
