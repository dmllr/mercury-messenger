package hela.eillance

import android.content.Intent
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.lifecycle.viewmodel.compose.viewModel
import hela.eillance.ndkio.CallDirection
import hela.eillance.screens.CallScreen
import hela.eillance.screens.IncomingCallScreen
import hela.eillance.theme.EillanceTheme

class IncomingCallActivity : ComponentActivity() {

  private var currentPeerId: String? = null

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    configureWindowForLockScreen()

    onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
      override fun handleOnBackPressed() {
        // No-op. The user must explicitly Accept or Reject.
      }
    })

    handleIntent(intent)
  }

  override fun onStart() {
    super.onStart()
    MessengerService.instance?.isIncomingUIVisible = true
  }

  override fun onStop() {
    super.onStop()
    MessengerService.instance?.isIncomingUIVisible = false
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    setIntent(intent)
    handleIntent(intent)
  }

  private fun handleIntent(intent: Intent) {
    currentPeerId = intent.getStringExtra(MessengerService.EXTRA_PEER_ID)

    // Refresh UI
    setContent {
      EillanceTheme {
        Surface(
          modifier = Modifier.fillMaxSize(),
          color = MaterialTheme.colorScheme.background
        ) {
          RenderCallScreen()
        }
      }
    }
  }

  @Composable
  private fun RenderCallScreen() {
    val service = MessengerService.instance
    if (service == null) {
      finishAndRemoveTask()
      return
    }

    val callVM: hela.eillance.viewmodels.CallViewModel = viewModel()

    val callState by callVM.callState.collectAsState()

    // If peer ID isn't set yet, try to get it from state
    val peerId = currentPeerId ?: callState.peerId
    if (peerId.isEmpty()) {
      // Wait briefly or exit if invalid state
      if (!callState.isRinging) {
        finishAndRemoveTask()
        return
      }
    }

    // Resolve contact info dynamically
    val contactInfo = remember(peerId) { service.resolveContactInfo(peerId) }

    // Auto-close logic: Keep open if Ringing, Connecting, or InCall
    val isActive = callState.isRinging || callState.isConnecting || callState.isInCall
    if (!isActive) {
      finishAndRemoveTask()
      return
    }

    // Security check: ensure we are showing the call we expect
    if (currentPeerId != null && callState.peerId != currentPeerId && callState.peerId.isNotEmpty()) {
      // State mismatch, but if valid call exists, we show it.
    } else if (callState.isRinging && callState.callDirection != CallDirection.Incoming) {
      // Unreachable
      finishAndRemoveTask()
      return
    }

    if (callState.isRinging && callState.callDirection == CallDirection.Incoming) {
      IncomingCallScreen(
        callerId = peerId,
        callerName = contactInfo?.displayName,
        callerPhoto = contactInfo?.photo,
        callType = callState.callType,
        callVM = callVM,
        onAccept = {
          service.acceptCall()
        },
        onReject = {
          service.rejectCall()
          finishAndRemoveTask()
        }
      )
    } else {
      // Transition to active call UI within this activity to stay on lock screen
      CallScreen(
        callVM = callVM,
        callState = callState,
        contactName = contactInfo?.displayName,
        contactPhoto = contactInfo?.photo
      )
    }
  }

  private fun configureWindowForLockScreen() {
    setShowWhenLocked(true)
    setTurnScreenOn(true)

    // Keyguard flags
    @Suppress("DEPRECATION")
    val flags = WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON or
        WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
        WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON or
        WindowManager.LayoutParams.FLAG_DISMISS_KEYGUARD

    window.addFlags(flags)
  }
}
