package hela.eillance.streamer

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.provider.Settings
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.core.content.ContextCompat
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import hela.eillance.camera.R

class StreamerActivity : ComponentActivity(), ServiceConnection {

  var streamerService: StreamerService? = null
  private var cameraServiceBound: Boolean = false

  private val requiredPermissions = mutableListOf(
    Manifest.permission.CAMERA,
    Manifest.permission.RECORD_AUDIO
  ).apply {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      add(Manifest.permission.POST_NOTIFICATIONS)
    }
  }

  private val permissionLauncher = registerForActivityResult(
    ActivityResultContracts.RequestMultiplePermissions()
  ) { permissions ->
    if (permissions.all { it.value }) {
      startStreamingService()
    } else {
      showSettingsRedirectDialog()
    }
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    setContent {
      MaterialTheme {
        Surface(
          modifier = Modifier.fillMaxSize(),
          color = MaterialTheme.colorScheme.background
        ) {
          StreamerApp(
            permissionsGranted = arePermissionsGranted(),
            onRequestPermissions = { checkAndRequestPermissions() },
            onSettingsClick = { openSettingsScreen() },
            streamerService = streamerService
          )
        }
      }
    }

    checkAndRequestPermissions()
  }

  @OptIn(ExperimentalMaterial3Api::class)
  @Composable
  fun StreamerApp(
    permissionsGranted: Boolean,
    onRequestPermissions: () -> Unit,
    onSettingsClick: () -> Unit,
    streamerService: StreamerService?
  ) {
    val navController = rememberNavController()
    val currentBackStackEntry by navController.currentBackStackEntryAsState()
    val showBackButton = currentBackStackEntry?.destination?.route != "operation"

    Scaffold(
      topBar = {
        TopAppBar(
          title = { Text(text = stringResource(id = R.string.app_name)) },
          navigationIcon = {
            if (showBackButton) {
              IconButton(onClick = { navController.popBackStack() }) {
                Icon(
                  painter = painterResource(id = R.drawable.ic_status2),
                  contentDescription = stringResource(id = android.R.string.cancel)
                )
              }
            }
          },
          actions = {
            IconButton(onClick = { showUserMenu(navController) }) {
              Icon(
                painter = painterResource(id = R.drawable.ic_status3),
                contentDescription = stringResource(id = R.string.toolbar_user_icon)
              )
            }
          }
        )
      }
    ) { paddingValues ->
      Box(modifier = Modifier.padding(paddingValues)) {
        if (!permissionsGranted) {
          PermissionsScreen(onRequestPermissions = onRequestPermissions)
        } else {
          NavigationGraph(
            navController = navController,
            streamerService = streamerService
          )
        }
      }
    }
  }

  @Composable
  private fun NavigationGraph(
    navController: NavHostController,
    streamerService: StreamerService?
  ) {
    NavHost(
      navController = navController,
      startDestination = "operation"
    ) {
      composable("operation") {
        OperationScreen(streamerService = streamerService)
      }
      composable("preferences") {
        CapturePreferencesScreen()
      }
    }
  }

  @Composable
  private fun PermissionsScreen(onRequestPermissions: () -> Unit) {
    Box(
      modifier = Modifier.fillMaxSize(),
      contentAlignment = Alignment.Center
    ) {
      Button(onClick = onRequestPermissions) {
        Text(text = stringResource(id = R.string.permissions_required))
      }
    }
  }

  private fun showUserMenu(navController: NavHostController) {
    // In Compose, we'd use a DropdownMenu instead of PopupMenu
    // For simplicity in this conversion, we'll just navigate to preferences
    navController.navigate("preferences")
  }

  private fun arePermissionsGranted(): Boolean {
    return requiredPermissions.all {
      ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }
  }

  private fun checkAndRequestPermissions() {
    if (!arePermissionsGranted()) {
      val shouldShowRationale = requiredPermissions.any {
        shouldShowRequestPermissionRationale(it)
      }

      if (shouldShowRationale) {
        showPermissionRationale(requiredPermissions.toTypedArray())
      } else {
        permissionLauncher.launch(requiredPermissions.toTypedArray())
      }
    } else {
      startStreamingService()
    }
  }

  private fun showPermissionRationale(permissions: Array<String>) {
    AlertDialog.Builder(this)
      .setTitle(getString(R.string.permissions_required_rationale_title))
      .setMessage(getString(R.string.permissions_required_rationale_text))
      .setPositiveButton(getString(R.string.grant_permissions)) { _, _ ->
        permissionLauncher.launch(permissions)
      }
      .setNegativeButton(getString(android.R.string.cancel)) { dialog, _ ->
        dialog.dismiss()
      }
      .setCancelable(false)
      .show()
  }

  private fun showSettingsRedirectDialog() {
    AlertDialog.Builder(this)
      .setTitle("Permissions Required")
      .setMessage("You have denied necessary permissions. To enable streaming, please allow them in settings.")
      .setPositiveButton("Open Settings") { _, _ ->
        openSettingsScreen()
      }
      .setNegativeButton("Cancel") { dialog, _ ->
        dialog.dismiss()
      }
      .show()
  }

  private fun openSettingsScreen() {
    val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
      data = Uri.parse("package:$packageName")
    }
    startActivity(intent)
  }

  private fun startStreamingService() {
    val streamerServiceIntent = Intent(applicationContext, StreamerService::class.java)
    startService(streamerServiceIntent)
    if (applicationContext.bindService(
        streamerServiceIntent,
        this,
        Context.BIND_AUTO_CREATE + Context.BIND_ABOVE_CLIENT
      )
    ) {
      cameraServiceBound = true
    }
  }

  override fun onDestroy() {
    unbindCameraService()
    super.onDestroy()
  }

  private fun unbindCameraService() {
    if (cameraServiceBound) {
      try {
        unbindService(this)
      } catch (_: IllegalArgumentException) {
        Log.e(javaClass.simpleName, "Trying to unbind service that is not registered")
      }
      cameraServiceBound = false
    }
  }

  // ServiceConnection implementation
  override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
    streamerService = (service as StreamerService.StreamingServiceBinder).service
  }

  override fun onServiceDisconnected(name: ComponentName?) {
    streamerService = null
  }
}
