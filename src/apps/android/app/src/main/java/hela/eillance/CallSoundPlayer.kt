package hela.eillance

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.MediaPlayer
import android.media.RingtoneManager
import android.os.Build
import android.os.VibrationAttributes
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import hela.eillance.camera.R
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallState

class CallSoundPlayer(private val context: Context) {
  private var mediaPlayer: MediaPlayer? = null
  private var currentSoundState: SoundState = SoundState.None

  private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
  private val vibrator: Vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
    (context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager).defaultVibrator
  } else {
    @Suppress("DEPRECATION")
    context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
  }

  private enum class SoundState {
    None,
    Connecting, // Outgoing connecting tone
    Ringback,   // Outgoing ringing tone (heard by caller)
    Ringing     // Incoming system ringtone (heard by receiver)
  }

  fun updateCallState(state: CallState?) {
    val targetState = when {
      state == null -> SoundState.None
      // Priority: Incoming Ringing must take precedence over Connecting
      state.isRinging && state.callDirection == CallDirection.Incoming -> SoundState.Ringing
      state.isRinging && state.callDirection == CallDirection.Outgoing -> SoundState.Ringback
      state.isConnecting -> SoundState.Connecting
      else -> SoundState.None
    }

    if (targetState != currentSoundState) {
      stop()
      currentSoundState = targetState
      startSound(targetState)
    }
  }

  private fun startSound(state: SoundState) {
    if (state == SoundState.None) return

    try {
      if (state == SoundState.Ringing) {
        startVibration()
      }

      if (state == SoundState.Ringing && audioManager.ringerMode != AudioManager.RINGER_MODE_NORMAL) {
        return
      }

      mediaPlayer = MediaPlayer()

      when (state) {
        SoundState.Connecting -> {
          // Play connecting tone on the voice call stream (usually earpiece)
          val afd = context.resources.openRawResourceFd(R.raw.connecting) ?: return
          mediaPlayer?.setDataSource(afd.fileDescriptor, afd.startOffset, afd.length)
          afd.close()

          mediaPlayer?.setAudioAttributes(
            AudioAttributes.Builder()
              .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION_SIGNALLING)
              .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
              .build()
          )
        }

        SoundState.Ringback -> {
          // Play ringback tone (standard phone ring) on voice stream (earpiece)
          val afd = context.resources.openRawResourceFd(R.raw.ringing) ?: return
          mediaPlayer?.setDataSource(afd.fileDescriptor, afd.startOffset, afd.length)
          afd.close()

          mediaPlayer?.setAudioAttributes(
            AudioAttributes.Builder()
              .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION_SIGNALLING)
              .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
              .build()
          )
        }

        SoundState.Ringing -> {
          // CRITICAL: Force audio mode to NORMAL, to prevent system from routing to earpiece
          // if it thinks a call is setting up (MODE_IN_COMMUNICATION).
          audioManager.mode = AudioManager.MODE_NORMAL

          // CRITICAL: Explicitly force speakerphone. This overrides "Voice Call" routing
          // logic if the system stubbornly stays in a communication mode.
          if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val currentDevice = audioManager.communicationDevice
            if (currentDevice?.type != AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) {
              val speakerDevice = audioManager.availableCommunicationDevices.firstOrNull {
                it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER
              }
              speakerDevice?.let { audioManager.setCommunicationDevice(it) }
            }
          } else {
            @Suppress("DEPRECATION")
            if (!audioManager.isSpeakerphoneOn) {
              audioManager.isSpeakerphoneOn = true
            }
          }

          // Play system default ringtone
          val ringtoneUri =
            RingtoneManager.getActualDefaultRingtoneUri(context, RingtoneManager.TYPE_RINGTONE)
              ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)

          if (ringtoneUri != null) {
            mediaPlayer?.setDataSource(context, ringtoneUri)
            // Explicitly removed deprecated setAudioStreamType.
            // setAudioAttributes implicitly handles the proper STREAM_RING routing.
            mediaPlayer?.setAudioAttributes(
              AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build()
            )
          } else {
            Log.w(TAG, "No default ringtone found.")
            mediaPlayer?.release()
            mediaPlayer = null
            return
          }
        }

        SoundState.None -> return
      }

      mediaPlayer?.isLooping = true
      mediaPlayer?.prepare()
      mediaPlayer?.start()

    } catch (e: Exception) {
      Log.e(TAG, "Error playing sound for state $state", e)
      stop()
    }
  }

  private fun startVibration() {
    if (audioManager.ringerMode == AudioManager.RINGER_MODE_SILENT) return

    // Standard call vibration pattern: Wait 0, Vibrate 1s, Sleep 1s
    val pattern = longArrayOf(0, 1000, 1000)

    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        val attributes = VibrationAttributes.createForUsage(VibrationAttributes.USAGE_RINGTONE)
        vibrator.vibrate(VibrationEffect.createWaveform(pattern, 0), attributes)
      } else {
        val effect = VibrationEffect.createWaveform(pattern, 0)
        vibrator.vibrate(effect)
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to start vibration", e)
    }
  }

  fun stop() {
    try {
      if (mediaPlayer?.isPlaying == true) {
        mediaPlayer?.stop()
      }
      mediaPlayer?.release()
    } catch (e: Exception) {
      Log.e(TAG, "Error stopping sound", e)
    } finally {
      mediaPlayer = null
      currentSoundState = SoundState.None
    }

    // Reset speakerphone if we forced it on for ringing
    // When the call is answered, CallHandler will set up the correct routing (usually Earpiece initially)
    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        val currentDevice = audioManager.communicationDevice
        if (currentDevice?.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) {
          audioManager.clearCommunicationDevice()
        }
      } else {
        @Suppress("DEPRECATION")
        if (audioManager.isSpeakerphoneOn) {
          audioManager.isSpeakerphoneOn = false
        }
      }
    } catch (e: Exception) {
      Log.e(TAG, "Error resetting speakerphone", e)
    }

    try {
      vibrator.cancel()
    } catch (e: Exception) {
      Log.e(TAG, "Error stopping vibration", e)
    }
  }

  companion object {
    private const val TAG = "CallSoundPlayer"
  }
}
