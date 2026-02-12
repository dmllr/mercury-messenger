package hela.eillance

import android.Manifest
import android.app.KeyguardManager
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
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

class MainActivity : ComponentActivity() {

  private val viewModel: MainViewModel by viewModels()

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    configureWindowForLockScreen()
    handleIntent(intent)
    viewModel.handleShareIntent(intent)

    // Start service immediately.
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

  private fun configureWindowForLockScreen() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
      setShowWhenLocked(true)
      setTurnScreenOn(true)
    } else {
      @Suppress("DEPRECATION")
      window.addFlags(
        WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
            WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON or
            WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
      )
    }

    val keyguardManager = getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
    if (keyguardManager.isKeyguardLocked) {
      keyguardManager.requestDismissKeyguard(this, null)
    }
  }

  override fun onStart() {
    super.onStart()
    MercuryApplication.isAppInForeground = true
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

  private fun handleIntent(intent: Intent?) {
    val peerId = intent?.getStringExtra(CallService.EXTRA_PEER_ID)
    val isAcceptAction = intent?.action == CallService.ACTION_ACCEPT_CALL
    val isShowIncomingCall = intent?.action == CallService.ACTION_SHOW_INCOMING_CALL

    if (isAcceptAction && peerId != null) {
      // Security check: Do not accept if permission is missing.
      // Instead, show the incoming call UI which handles the error state.
      if (ContextCompat.checkSelfPermission(
          this,
          Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED
      ) {
        CallService.instance?.acceptCall()
        viewModel.navigateToPeer(peerId)
      } else {
        configureWindowForLockScreen()
        viewModel.showIncomingCallUi()
      }
    } else if (isShowIncomingCall && peerId != null) {
      configureWindowForLockScreen()
      viewModel.showIncomingCallUi()
    } else if (peerId != null) {
      viewModel.navigateToPeer(peerId)
    }
  }

  private fun startService() {
    Intent(this, CallService::class.java).also { intent ->
      startForegroundService(intent)
    }
  }
}
