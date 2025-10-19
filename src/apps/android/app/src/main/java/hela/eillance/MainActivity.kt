package hela.eillance

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import hela.eillance.theme.EillanceTheme

class MainActivity : ComponentActivity() {

  private val viewModel: MainViewModel by viewModels()
  private var callService: CallService? = null

  private val serviceConnection = object : ServiceConnection {
    override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
      val binder = service as CallService.CallServiceBinder
      callService = binder.getService()
      viewModel.setService(callService)
    }

    override fun onServiceDisconnected(name: ComponentName?) {
      callService = null
      viewModel.setService(null)
    }
  }

  private val requestPermissionLauncher =
    registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
      if (permissions.values.all { it }) {
        startAndBindService()
      } else {
        Toast.makeText(this, "Camera and microphone permissions are required to make calls.", Toast.LENGTH_LONG).show()
      }
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    checkPermissionsAndStart()

    setContent {
      EillanceTheme {
        Surface(
          modifier = Modifier.fillMaxSize(),
          color = MaterialTheme.colorScheme.background
        ) {
          val navController = rememberNavController()
          val peerList by viewModel.peerList.collectAsState()

          NavHost(navController = navController, startDestination = "peers") {
            composable("peers") {
              PeerListScreen(
                peers = peerList,
                viewModel = viewModel,
                onPeerClick = { peerId ->
                  callService?.call(peerId)
                  navController.navigate("call/$peerId")
                }
              )
            }
            composable("call/{peerId}") { backStackEntry ->
              val peerId = backStackEntry.arguments?.getString("peerId")
              if (peerId != null) {
                CallScreen(
                  peerId = peerId,
                  onHangup = {
                    navController.popBackStack()
                  },
                  initDecoder = { surface -> callService?.initDecoder(surface) },
                  releaseDecoder = { callService?.releaseDecoder() }
                )
              }
            }
          }
        }
      }
    }
  }

  private fun checkPermissionsAndStart() {
    val requiredPermissions = mutableListOf(
      Manifest.permission.CAMERA,
      Manifest.permission.RECORD_AUDIO
    )
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      requiredPermissions.add(Manifest.permission.POST_NOTIFICATIONS)
    }

    val allGranted = requiredPermissions.all {
      ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }

    if (allGranted) {
      startAndBindService()
    } else {
      requestPermissionLauncher.launch(requiredPermissions.toTypedArray())
    }
  }

  private fun startAndBindService() {
    Intent(this, CallService::class.java).also { intent ->
      startForegroundService(intent)
      bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
    }
  }

  override fun onDestroy() {
    super.onDestroy()
    unbindService(serviceConnection)
  }
}
