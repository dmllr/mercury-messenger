package hela.eillance

import android.Manifest
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import hela.eillance.theme.EillanceTheme

class MessengerActivity : ComponentActivity() {

  private val viewModel: MainViewModel by viewModels()

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    updateLockScreenPolicy()
    handleIntent(intent)
    viewModel.handleShareIntent(intent)

    // Start service immediately to ensure connection is maintained.
    startService()

    setContent {
      EillanceTheme {
        Surface(
          modifier = Modifier.fillMaxSize(),
          color = MaterialTheme.colorScheme.background,
        ) {
          MercuryApp(viewModel = viewModel)
        }
      }
    }
  }

  override fun onStart() {
    super.onStart()
    MercuryApplication.isAppInForeground = true

    // Clear any "New Message" or "Missed Call" notifications when user enters the app.
    val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    notificationManager.cancelAll()
  }

  override fun onResume() {
    super.onResume()
    viewModel.checkAllPermissions()
  }

  override fun onStop() {
    super.onStop()
    MercuryApplication.isAppInForeground = false
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    setIntent(intent)
    handleIntent(intent)
    viewModel.handleShareIntent(intent)
  }

  private fun updateLockScreenPolicy() {
    val callState = MessengerService.instance?.backgroundCallVM?.callState?.value
    val isActiveCall = callState?.isInCall == true || callState?.isConnecting == true

    setShowWhenLocked(isActiveCall)
    setTurnScreenOn(isActiveCall)
    if (isActiveCall) {
      window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    } else {
      window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
  }

  private fun handleIntent(intent: Intent?) {
    val peerId = intent?.getStringExtra(MessengerService.EXTRA_PEER_ID)
    val isAcceptAction = intent?.action == MessengerService.ACTION_ACCEPT_CALL
    val isShowIncomingCall = intent?.action == MessengerService.ACTION_SHOW_INCOMING_CALL

    if (isAcceptAction && peerId != null) {
      // Security check: Do not accept if permission is missing.
      // Instead, show the incoming call UI which handles the error state.
      if (ContextCompat.checkSelfPermission(
          this,
          Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED
      ) {
        MessengerService.instance?.acceptCall()
        viewModel.navigateToPeer(peerId)
        updateLockScreenPolicy()
      } else {
        viewModel.showIncomingCallUi()
      }
    } else if (isShowIncomingCall && peerId != null) {
      viewModel.showIncomingCallUi()
    } else if (peerId != null) {
      viewModel.navigateToPeer(peerId)
    }
  }

  private fun startService() {
    Intent(this, MessengerService::class.java).also { intent ->
      startForegroundService(intent)
    }
  }
}
