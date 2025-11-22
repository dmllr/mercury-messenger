package hela.eillance

import android.content.Context
import android.media.MediaPlayer
import android.util.Log
import hela.eillance.camera.R
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallState

class CallSoundPlayer(private val context: Context) {
  private var mediaPlayer: MediaPlayer? = null
  private var currentSoundResId: Int? = null

  fun updateCallState(state: CallState?) {
    // Determine which sound, if any, should be playing
    val targetSoundResId = when {
      state == null -> null
      state.isConnecting -> R.raw.connecting
      // Play ringing sound only for outgoing calls (ringback tone)
      state.isRinging && state.callDirection == CallDirection.Outgoing -> R.raw.ringing
      else -> null
    }

    // If the required sound has changed, stop the current one and start the new one
    if (targetSoundResId != currentSoundResId) {
      stop()
      if (targetSoundResId != null) {
        playSound(targetSoundResId)
      }
      currentSoundResId = targetSoundResId
    }
  }

  private fun playSound(resId: Int) {
    try {
      mediaPlayer = MediaPlayer.create(context, resId)?.apply {
        isLooping = true
        start()
      }
      if (mediaPlayer == null) {
        Log.e("CallSoundPlayer", "Failed to create MediaPlayer for resource $resId")
      }
    } catch (e: Exception) {
      Log.e("CallSoundPlayer", "Error playing sound", e)
    }
  }

  fun stop() {
    try {
      if (mediaPlayer?.isPlaying == true) {
        mediaPlayer?.stop()
      }
      mediaPlayer?.release()
    } catch (e: Exception) {
      Log.e("CallSoundPlayer", "Error stopping sound", e)
    } finally {
      mediaPlayer = null
      currentSoundResId = null
    }
  }
}
