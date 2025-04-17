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
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.Icons.Filled
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
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
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.content.edit
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.ViewModelProvider
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.google.firebase.auth.FirebaseAuth
import hela.eillance.LauncherActivity
import hela.eillance.camera.R
import hela.eillance.ndkio.Config
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.Peer
import hela.eillance.ndkio.Profile
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.ndkio.interfaces.IViewerNativeBridge
import hela.eillance.theme.EillanceTheme
import hela.eillance.theme.extendedColors
import hela.eillance.theme.icons.AppIcons
import hela.eillance.theme.icons.Failure
import hela.eillance.theme.icons.Signout
import hela.eillance.theme.icons.Status2
import hela.eillance.theme.icons.StatusStandby
import hela.eillance.theme.icons.SwitchRole
import kotlinx.coroutines.launch

class ViewerActivity : ComponentActivity(), IViewerNativeBridge {

  private lateinit var navController: NavHostController
  private val viewModel: ViewerViewModel by viewModels {
    ViewModelProvider.AndroidViewModelFactory.getInstance(application)
  }

  // Track pull-to-refresh
  private var isRefreshingPeers by mutableStateOf(false)
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

  private external fun initEngine(nativeBridge: IViewerNativeBridge)
  private external fun initDecoder(surface: Surface)
  private external fun releaseDecoder()
  private external fun updateAuthToken(token: String)
  private external fun requestPeerListUpdate()
  external fun sendStreamRequest(peerId: String, config: Config)
  external fun sendConfigurationRequest(peerId: String, config: Config)

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
  private fun ConnectionStatusDisplay(connectionState: ServiceConnectionState) {
    val context = LocalContext.current
    val text = connectionState.getResourceString(context)
    val tint = when (connectionState) {
      ServiceConnectionState.Online -> MaterialTheme.extendedColors.success
      else -> MaterialTheme.extendedColors.warning
    }

    Row(
      verticalAlignment = Alignment.CenterVertically,
      modifier = Modifier.padding(top = 2.dp)
    ) {
      // Online/Offline Indicator Icon
      Icon(
        imageVector = when (connectionState) {
          ServiceConnectionState.Online -> AppIcons.StatusStandby
          ServiceConnectionState.Failure -> AppIcons.Failure
          else -> AppIcons.Status2
        },
        contentDescription = text,
        tint = tint,
        modifier = Modifier.size(14.dp)
      )
      Spacer(Modifier.width(4.dp))
      Text(
        text = text,
        style = MaterialTheme.typography.bodySmall,
        color = tint,
        fontStyle = FontStyle.Italic
      )
    }
  }

  @Composable
  private fun UserMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    onReselectRole: () -> Unit,
    onSignOut: () -> Unit
  ) {
    DropdownMenu(
      expanded = expanded,
      onDismissRequest = onDismissRequest
    ) {
      DropdownMenuItem(
        text = { Text(stringResource(id = R.string.menu_reselect_role)) },
        onClick = onReselectRole,
        leadingIcon = {
          Icon(
            imageVector = AppIcons.SwitchRole,
            contentDescription = stringResource(id = R.string.menu_reselect_role)
          )
        }
      )
      HorizontalDivider()
      DropdownMenuItem(
        text = { Text(stringResource(id = R.string.menu_sign_out)) },
        onClick = onSignOut,
        leadingIcon = {
          Icon(
            imageVector = AppIcons.Signout,
            contentDescription = stringResource(id = R.string.menu_sign_out)
          )
        }
      )
    }
  }

  @Composable
  fun AppContent(navController: NavHostController, viewModel: ViewerViewModel) {
    val currentRoute = navController.currentBackStackEntryAsState().value?.destination?.route
    val displayedPeers by viewModel.displayedPeers.collectAsState()
    val connectionStatusState = viewModel.connectionStatus.observeAsState(ServiceConnectionState.Unknown.ordinal)
    val connectionState = ServiceConnectionState.fromInt(connectionStatusState.value)
    val scope = rememberCoroutineScope()

    val refreshPeersAction: () -> Unit = {
      if (!isRefreshingPeers) {
        scope.launch {
          isRefreshingPeers = true
          requestPeerListUpdate()
        }
      }
    }

    EillanceTheme {
      Scaffold(
        topBar = {
          if (!isFullscreen) {
            AppTopBar(
              currentRoute,
              navController,
              viewModel,
              onReselectRole = { launchRoleSelection() },
              onSignOut = { signOut() }
            )
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
              NavHost(navController = navController, startDestination = "peers") {
                composable("peers") {
                  PeersScreen(
                    peers = displayedPeers,
                    onPeerClicked = { storedPeer ->
                      viewModel.selectPeerById(storedPeer.id)
                      if (viewModel.selectedPeer != null) {
                        navController.navigate("stream")
                      } else {
                        Log.w("ViewerActivity", "Navigation to stream cancelled, peer ${storedPeer.id} could not be selected.")
                      }
                    },
                    onRemovePeer = { peerIdToRemove ->
                      viewModel.removePeer(peerIdToRemove)
                    },
                    isRefreshing = isRefreshingPeers,
                    onRefreshPeers = refreshPeersAction
                  )
                }
                composable("stream") {
                  val currentSelectedPeer = viewModel.selectedPeer
                  if (currentSelectedPeer != null) {
                    StreamScreen(
                      peer = currentSelectedPeer,
                      initDecoder = ::initDecoder,
                      releaseDecoder = ::releaseDecoder,
                      viewModel = viewModel,
                    )
                  } else {
                    LaunchedEffect(Unit) {
                      Log.w("ViewerActivity", "StreamScreen composed with null selectedPeer, navigating back.")
                      navController.popBackStack()
                    }
                  }
                }
              }
            }
            ServiceConnectionState.Failure -> {
              ErrorScreen("Connection failed. Please check your network and try again.", onRetry = {
                Log.d("ViewerActivity", "Retry button clicked - Re-initializing engine potentially?")
                // TODO better retry
                // For now, just restart the activity
                val intent = Intent(this@ViewerActivity, ViewerActivity::class.java)
                intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_NEW_TASK)
                startActivity(intent)
                finish()
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
    viewModel: ViewerViewModel,
    onReselectRole: () -> Unit,
    onSignOut: () -> Unit
  ) {
    val connectionStatusState = viewModel.connectionStatus.observeAsState(ServiceConnectionState.Unknown.ordinal)
    val connectionState = ServiceConnectionState.fromInt(connectionStatusState.value)
    var menuExpanded by remember { mutableStateOf(false) }

    TopAppBar(
      title = {
        Column {
          Text(
            when (currentRoute) {
              "peers" -> "Available Devices"
              "stream" -> viewModel.selectedPeer?.name ?: "Stream"
              else -> "Surveillance Viewer"
            }
          )
          ConnectionStatusDisplay(connectionState = connectionState)
        }
      },
      navigationIcon = {
        if (currentRoute != "peers") {
          IconButton(onClick = { navController.popBackStack() }) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
          }
        }
      },
      actions = {
        Box {
          IconButton(onClick = { menuExpanded = true }) {
            Icon(
              imageVector = Icons.Default.Settings,
              contentDescription = "Settings"
            )
          }
          UserMenu(
            expanded = menuExpanded,
            onDismissRequest = { menuExpanded = false },
            onReselectRole = {
              onReselectRole()
              menuExpanded = false
            },
            onSignOut = {
              onSignOut()
              menuExpanded = false
            }
          )
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

  private fun launchRoleSelection() {
    // Clear the role preference
    getSharedPreferences("app_prefs", MODE_PRIVATE).edit {
      remove("role")
      putBoolean("ask_each_time", true)
    }

    // Launch the LauncherActivity
    val intent = Intent(this, LauncherActivity::class.java)
    intent.flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
    startActivity(intent)
    finish()
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

  private fun signOut() {
    FirebaseAuth.getInstance().signOut()
    updateAuthToken("")

    // Navigate back to launcher
    val intent = Intent(this, LauncherActivity::class.java)
    intent.flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
    startActivity(intent)
    finish()
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

  override fun onStatusUpdate(peerId: String, source: Int, status: Int) {
    val eventSource = EventSource.fromInt(source)
    Log.d("ViewerActivity", "onStatusUpdate: Peer=$peerId, Source=$eventSource, Status=$status")
    viewModel.updatePeerStatus(peerId, eventSource, status)

    // Handle token invalidation
    if (source == EventSource.System.value && status == ServiceConnectionState.InvalidToken.value) {
      refreshAuthToken()
    }
  }

  override fun onStreamInfoChange(width: Int, height: Int, rotation: Int) {
    Log.d("ViewerActivity", "onStreamInfoChange: W=$width, H=$height, R=$rotation")
    viewModel.updateStreamInfo(width, height, rotation)
  }

  override fun onDeviceProfileReceived(peerId: String, profile: Profile) {
    Log.d("ViewerActivity", "onDeviceProfileReceived: Peer=$peerId, Profile Cameras=${profile.size}")
    viewModel.updateDeviceProfile(profile)
  }

  override fun onPeerListUpdate(peers: List<Peer>) {
    Log.d("ViewerActivity", "onPeerListUpdate: Received ${peers.size} peers.")
    viewModel.updatePeerList(peers)
    if (isRefreshingPeers) {
      isRefreshingPeers = false
    }
  }

  private fun refreshAuthToken() {
    val user = FirebaseAuth.getInstance().currentUser
    user?.getIdToken(true)?.addOnCompleteListener { task ->
      if (task.isSuccessful) {
        val idToken = task.result?.token
        if (idToken != null) {
          updateAuthToken(idToken)
          Log.d("ViewerActivity", "Auth token refreshed successfully")
        }
      } else {
        Log.e("ViewerActivity", "Failed to refresh auth token", task.exception)
        // Token refresh failed, redirect to launcher for re-authentication
        startActivity(Intent(this, LauncherActivity::class.java))
        finish()
      }
    }
  }
}
