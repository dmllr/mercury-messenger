package hela.eillance.viewer

import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.Icons.Filled
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.google.firebase.auth.FirebaseAuth
import hela.eillance.LauncherActivity
import hela.eillance.ndkio.Config
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.Peer
import hela.eillance.ndkio.Profile
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.ndkio.interfaces.IViewerNativeBridge
import hela.eillance.theme.EillanceTheme

class ViewerActivity : ComponentActivity(), IViewerNativeBridge {

  private lateinit var navController: NavHostController
  private val viewModel: ViewerViewModel by viewModels()

  // Track fullscreen mode state
  private var isFullscreen by mutableStateOf(false)

    private val backPressedCallback = object : OnBackPressedCallback(true) {
        override fun handleOnBackPressed() {
            // First check if we're in fullscreen mode
            if (isFullscreen) {
                setFullscreenMode(false)
            } else {
                // If not in fullscreen, let the system handle the back press
                isEnabled = false // Disable this callback temporarily
                onBackPressedDispatcher.onBackPressed() // Trigger the default back press
                isEnabled = true
            }
        }
    }

  companion object {
    init {
      System.loadLibrary("lib-viewer-service-bridge")
    }
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    enableEdgeToEdge()

    onBackPressedDispatcher.addCallback(this, backPressedCallback)

    initEngine(this)

    setContent {
      navController = rememberNavController()
      AppContent(navController, viewModel)
    }
  }

  @Composable
  fun AppContent(navController: NavHostController, viewModel: ViewerViewModel) {
    val currentRoute = navController.currentBackStackEntryAsState().value?.destination?.route

    // Observe the peers list (this is already a Compose State)
    val peers = viewModel.peers

    // Observe the selected peer (this is already a Compose State)
    val selectedPeer = viewModel.selectedPeer

    // Observe connection status directly - this will refresh UI when connection state changes
    val connectionStatusState = viewModel.connectionStatus.observeAsState(ServiceConnectionState.Unknown.ordinal)

    // Convert to ServiceConnectionState enum
    val connectionState = ServiceConnectionState.fromInt(connectionStatusState.value)

    // Still observe peer status for peer-specific events, if needed
    val peerStatusState = viewModel.peerStatus.observeAsState()

    EillanceTheme {
      Scaffold(
        topBar = {
          // Only show the TopAppBar when not in fullscreen mode
          if (!isFullscreen) {
            AppTopBar(currentRoute, navController, viewModel)
          }
        }
      ) { paddingValues ->
        Surface(
          modifier = Modifier
            .fillMaxSize()
            .padding(paddingValues.takeIf { !isFullscreen } ?: PaddingValues(0.dp)),
          color = MaterialTheme.colorScheme.background
        ) {
          when (connectionState) {
            ServiceConnectionState.Online -> {
              // Show normal content
              NavHost(navController = navController, startDestination = "peers") {
                composable("peers") {
                  PeersScreen(
                    peers = peers,
                    onPeerClicked = { peer ->
                      viewModel.selectedPeer = peer
                      navController.navigate("stream")
                    }
                  )
                }
                composable("stream") {
                  if (selectedPeer != null) {
                    StreamScreen(
                      peer = selectedPeer,
                      initDecoder = { surface -> initDecoder(surface) },
                      releaseDecoder = { releaseDecoder() },
                      viewModel = viewModel
                    )
                  } else {
                    // Handle case where selectedPeer is null
                    LaunchedEffect(Unit) {
                      navController.popBackStack()
                    }
                  }
                }
              }
            }
            ServiceConnectionState.Failure -> {
              ErrorScreen("Connection failed. Please check your network and try again.", onRetry = {
                TODO()
              })
            }
            else -> {
              StatusScreen("Status: $connectionState")
            }
          }
        }
      }
    }
  }

  @OptIn(ExperimentalMaterial3Api::class)
  @Composable
  fun AppTopBar(
    currentRoute: String?,
    navController: NavHostController,
    viewModel: ViewerViewModel
  ) {
    TopAppBar(
      title = {
        Text(
          when (currentRoute) {
            "peers" -> "Available Devices"
            "stream" -> viewModel.selectedPeer?.name ?: "Stream"
            else -> "Surveillance Viewer"
          }
        )
      },
      navigationIcon = {
        if (currentRoute != "peers") {
          IconButton(onClick = { navController.popBackStack() }) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
          }
        }
      }
    )
  }

  @Composable
  fun ErrorScreen(errorMessage: String, onRetry: () -> Unit) {
    Box(
      modifier = Modifier
        .fillMaxSize()
        .padding(16.dp),
      contentAlignment = Alignment.Center
    ) {
      Column(
        horizontalAlignment = Alignment.CenterHorizontally
      ) {
        Text(
          text = errorMessage,
          color = Color.Red,
          textAlign = TextAlign.Center,
          modifier = Modifier.padding(bottom = 16.dp)
        )

        OutlinedButton(
          onClick = {
            onRetry()
          },
        ) {
          Icon(
            imageVector = Filled.Refresh,
            contentDescription = "Retry",
            modifier = Modifier.padding(end = 8.dp)
          )
          Text("Retry")
        }
      }
    }
  }

  @Composable
  fun StatusScreen(statusMessage: String) {
    Text(
      text = statusMessage,
      color = Color.Red,
      textAlign = TextAlign.Center,
      modifier = Modifier.padding(bottom = 16.dp)
    )
  }

  fun showNetworkErrorScreen() {
    runOnUiThread {
      val initializationError =
        "No internet connection. Please check your connection and try again."
      setContent {
        ErrorScreen(initializationError, onRetry = {})
      }
    }
  }

  // Method to set fullscreen mode
  fun setFullscreenMode(fullscreen: Boolean) {
    isFullscreen = fullscreen

    val windowInsetsController = WindowInsetsControllerCompat(window, window.decorView)

    if (fullscreen) {
      // Hide the system bars
      windowInsetsController.hide(WindowInsetsCompat.Type.systemBars())
      windowInsetsController.systemBarsBehavior =
        WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE

      // Keep screen on during fullscreen mode
      window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    } else {
      // Show the system bars
      windowInsetsController.show(WindowInsetsCompat.Type.systemBars())

      // Remove keep screen on flag
      window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
  }

  override fun onResume() {
    super.onResume()

    val user = FirebaseAuth.getInstance().currentUser
    user?.getIdToken(true)?.addOnCompleteListener { task ->
      if (task.isSuccessful) {
        val idToken = task.result?.token
        if (idToken != null) {
          updateAuthToken(idToken)
        }
      } else {
        Log.e("FirebaseAuth", "Failed to get ID token", task.exception)
        startActivity(Intent(this, LauncherActivity::class.java))
        finish()
      }
    }
  }

  override fun onPause() {
    super.onPause()

    updateAuthToken("")

    // Exit fullscreen mode when activity is paused
    if (isFullscreen) {
      setFullscreenMode(false)
    }
  }

  private external fun initEngine(nativeBridge: IViewerNativeBridge)
  private external fun initDecoder(surface: Surface)
  private external fun releaseDecoder()
  private external fun updateAuthToken(token: String)
  external fun sendStreamRequest(peerId: String, config: Config)
  external fun sendConfigurationRequest(peerId: String, config: Config)

  override fun onStatusUpdate(peerId: String, source: Int, status: Int) {
    val eventSource = EventSource.fromInt(source)
    viewModel.updatePeerStatus(peerId, eventSource, status)
  }

  override fun onStreamInfoChange(width: Int, height: Int, rotation: Int) {
    viewModel.updateStreamInfo(width, height, rotation)
  }

  override fun onDeviceProfileReceived(peerId: String, profile: Profile) {
    viewModel.updateDeviceProfile(profile)
  }

  override fun onPeerListUpdate(peers: List<Peer>) {
    viewModel.peers = peers
  }

}
