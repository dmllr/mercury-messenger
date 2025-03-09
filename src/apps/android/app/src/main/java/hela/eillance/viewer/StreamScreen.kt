package hela.eillance.viewer

import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.constraintlayout.compose.ConstraintLayout
import androidx.lifecycle.viewmodel.compose.viewModel
import hela.eillance.camera.R
import hela.eillance.ndkio.Config
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.Peer
import hela.eillance.ndkio.Profile
import hela.eillance.ndkio.ServiceConnectionState

private const val RESOLUTION_SEPARATOR = "×"

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StreamScreen(
  peer: Peer,
  initDecoder: (Surface) -> Unit,
  releaseDecoder: () -> Unit,
  viewModel: ViewerViewModel = viewModel()
) {
  val context = LocalContext.current
  val defaultCameraLabel = stringResource(R.string.viewer_StreamScreen_default_camera)
  val defaultResolutionsLabel = stringResource(R.string.viewer_StreamScreen_default_resolution)
  val status by viewModel.peerStatus.observeAsState()
  val resolution by viewModel.streamResolution.observeAsState()
  val profile by viewModel.deviceProfile.observeAsState()

  var peerConnectionState by remember { mutableStateOf(ServiceConnectionState.Unknown) }
  var deviceProfile by remember { mutableStateOf(peer.profile) }
  var statusText by remember { mutableStateOf("") }

  // Camera selection state
  var cameraOptions by remember { mutableStateOf(listOf(defaultCameraLabel)) }
  var selectedCameraIndex by remember { mutableIntStateOf(0) }
  var expandedCamera by remember { mutableStateOf(false) }

  // Resolution selection state
  var resolutionOptions by remember { mutableStateOf(listOf(defaultResolutionsLabel)) }
  var selectedResolutionIndex by remember { mutableIntStateOf(0) }
  var expandedResolution by remember { mutableStateOf(false) }

  // Fullscreen state - starts as true by default
  var isFullscreen by remember { mutableStateOf(true) }

  // Detect current orientation from configuration
  val configuration = LocalContext.current.resources.configuration
  val isPortrait = configuration.orientation == android.content.res.Configuration.ORIENTATION_PORTRAIT

  // Remember the current video dimensions for proper aspect ratio calculation
  var videoWidth by remember { mutableIntStateOf(0) }
  var videoHeight by remember { mutableIntStateOf(0) }

  // Update camera options when profile changes
  fun updateCameraOptions(profile: Profile) {
    val names = profile.map { it.name }
    cameraOptions = names.ifEmpty { listOf(defaultCameraLabel) }
  }

  // Update resolution options when camera selection changes
  fun updateResolutionOptions(cameraIndex: Int) {
    if (cameraIndex in deviceProfile.indices) {
      val resolutions = deviceProfile[cameraIndex].resolutions
      resolutionOptions = resolutions.map { "${it.width}$RESOLUTION_SEPARATOR${it.height}" }
    } else {
      resolutionOptions = listOf(defaultResolutionsLabel)
    }
    selectedResolutionIndex = 0
  }

  // Create configuration for stream requests
  fun composeConfigurationMessage(): Config {
    val config = Config()
    if (selectedCameraIndex in deviceProfile.indices) {
      val camera = deviceProfile[selectedCameraIndex]
      config.video.cameraId = selectedCameraIndex

      if (selectedResolutionIndex in camera.resolutions.indices) {
        val resolution = camera.resolutions[selectedResolutionIndex]
        config.video.width = resolution.width
        config.video.height = resolution.height
      }
    }
    return config
  }

  // Send configuration request
  fun sendConfigurationRequest() {
    if (peerConnectionState == ServiceConnectionState.Online) {
      (context as? ViewerActivity)?.sendConfigurationRequest(peer.id, composeConfigurationMessage())
    }
  }

  // Handle status updates
  LaunchedEffect(status) {
    status?.let { (source, statusCode) ->
      if (source == EventSource.PeerConnectionState) {
        peerConnectionState = ServiceConnectionState.fromInt(statusCode)
      }
      statusText = "${source.name}: ${peerConnectionState.getResourceString(context)}"
    }
  }

  // Handle resolution updates
  LaunchedEffect(resolution) {
    resolution?.let { (width, height) ->
      if (width > 0 && height > 0) {
        videoWidth = width
        videoHeight = height
      }
    }
  }

  // Handle profile updates
  LaunchedEffect(profile) {
    profile?.let {
      deviceProfile = it
      updateCameraOptions(it)

      // If there are cameras available, select the first one automatically
      if (it.isNotEmpty()) {
        selectedCameraIndex = 0
        updateResolutionOptions(0)
      }
    }
  }

  // Initial stream request on composition
  LaunchedEffect(true) {
    (context as? ViewerActivity)?.sendStreamRequest(peer.id, composeConfigurationMessage())
  }

  // Set fullscreen mode when the screen opens
  LaunchedEffect(true) {
    (context as? ViewerActivity)?.setFullscreenMode(true)
  }

  // Notify the activity about fullscreen state changes
  LaunchedEffect(isFullscreen) {
    (context as? ViewerActivity)?.setFullscreenMode(isFullscreen)
  }

  ConstraintLayout(
    modifier = Modifier
      .fillMaxSize()
  ) {
    val (surface, controlsOverlay) = createRefs()

    // Surface view for video output
    AndroidView(
      factory = { ctx ->
        SurfaceView(ctx).apply {
          holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
              initDecoder(holder.surface)
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
              releaseDecoder()
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
              if (videoWidth <= 0 || videoHeight <= 0) return

              val deviceIsPortrait = height > width
              val videoIsLandscape = videoWidth > videoHeight

              if (deviceIsPortrait && videoIsLandscape) {
                // In portrait mode, when video is landscape, we need to calculate dimensions
                // to account for the 90-degree rotation by using the reversed aspect ratio

                // For portrait orientation with landscape video, use height as the fixed dimension
                // and calculate width based on the video's height-to-width ratio
                val scaledWidth = (height * (videoHeight.toFloat() / videoWidth.toFloat())).toInt()

                if (scaledWidth <= width) {
                  // If calculated width fits within available width, use it
                  holder.setFixedSize(scaledWidth, height)
                } else {
                  // Otherwise, use width as the fixed dimension and scale height
                  val scaledHeight = (width * (videoWidth.toFloat() / videoHeight.toFloat())).toInt()
                  holder.setFixedSize(width, scaledHeight)
                }
              } else if (!deviceIsPortrait && !videoIsLandscape) {
                // Landscape device with portrait video
                val scaledHeight = (width * (videoHeight.toFloat() / videoWidth.toFloat())).toInt()

                if (scaledHeight <= height) {
                  holder.setFixedSize(width, scaledHeight)
                } else {
                  val scaledWidth = (height * (videoWidth.toFloat() / videoHeight.toFloat())).toInt()
                  holder.setFixedSize(scaledWidth, height)
                }
              } else {
                // Normal case: orientations match
                val videoAspectRatio = videoWidth.toFloat() / videoHeight.toFloat()
                val surfaceAspectRatio = width.toFloat() / height.toFloat()

                if (surfaceAspectRatio > videoAspectRatio) {
                  // Surface is wider than needed
                  val scaledWidth = (height * videoAspectRatio).toInt()
                  holder.setFixedSize(scaledWidth, height)
                } else {
                  // Surface is taller than needed
                  val scaledHeight = (width / videoAspectRatio).toInt()
                  holder.setFixedSize(width, scaledHeight)
                }
              }
            }
          })
        }
      },
      modifier = Modifier
        .constrainAs(surface) {
          top.linkTo(parent.top)
          bottom.linkTo(parent.bottom)
          start.linkTo(parent.start)
          end.linkTo(parent.end)
        }
        .fillMaxSize()
    )

    // Semi-transparent overlay for controls
    Box(
      modifier = Modifier
        .fillMaxSize()
        .constrainAs(controlsOverlay) {
          top.linkTo(parent.top)
          bottom.linkTo(parent.bottom)
          start.linkTo(parent.start)
          end.linkTo(parent.end)
        }
    ) {
      val transparentTextFieldColors = ExposedDropdownMenuDefaults.textFieldColors(
        unfocusedIndicatorColor = Color.Transparent,
        focusedIndicatorColor = Color.Transparent,
        unfocusedContainerColor = Color.Transparent,
        focusedContainerColor = Color.Transparent
      )

      // Back button in top-left corner
      IconButton(
        onClick = {
          // Exit fullscreen mode and let the activity handle back navigation
          isFullscreen = false
          (context as? ViewerActivity)?.onBackPressedDispatcher?.onBackPressed()
        },
        modifier = Modifier
          .padding(16.dp)
          .align(Alignment.TopStart)
      ) {
        Icon(
          imageVector = Icons.AutoMirrored.Filled.ArrowBack,
          contentDescription = "Back",
          tint = Color.White
        )
      }

      // Status text in center
      if (statusText.isNotEmpty()) {
        Text(
          text = statusText,
          color = Color.White,
          modifier = Modifier
            .background(Color.Black.copy(alpha = 0.5f), shape = RoundedCornerShape(4.dp))
            .padding(8.dp)
            .align(Alignment.Center)
        )
      }

      // Control elements at bottom
      Row(
        modifier = Modifier
          .fillMaxWidth()
          .background(Color.Black.copy(alpha = 0.5f))
          .padding(8.dp)
          .align(Alignment.BottomCenter),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically
      ) {
        // Camera selection dropdown
        ExposedDropdownMenuBox(
          expanded = expandedCamera,
          onExpandedChange = { expandedCamera = it }
        ) {
          TextField(
            value = cameraOptions.getOrElse(selectedCameraIndex) { "Default Camera" },
            onValueChange = {},
            readOnly = true,
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expandedCamera) },
            colors = transparentTextFieldColors,
            modifier = Modifier.menuAnchor()
          )

          DropdownMenu(
            expanded = expandedCamera,
            onDismissRequest = { expandedCamera = false }
          ) {
            cameraOptions.forEachIndexed { index, option ->
              DropdownMenuItem(
                text = { Text(option) },
                onClick = {
                  selectedCameraIndex = index
                  expandedCamera = false
                  updateResolutionOptions(index)
                  sendConfigurationRequest()
                }
              )
            }
          }
        }

        // Resolution selection dropdown
        ExposedDropdownMenuBox(
          expanded = expandedResolution,
          onExpandedChange = { expandedResolution = it }
        ) {
          TextField(
            value = resolutionOptions.getOrElse(selectedResolutionIndex) { "Default Resolution" },
            onValueChange = {},
            readOnly = true,
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expandedResolution) },
            colors = transparentTextFieldColors,
            modifier = Modifier.menuAnchor()
          )

          DropdownMenu(
            expanded = expandedResolution,
            onDismissRequest = { expandedResolution = false }
          ) {
            resolutionOptions.forEachIndexed { index, option ->
              DropdownMenuItem(
                text = { Text(option) },
                onClick = {
                  selectedResolutionIndex = index
                  expandedResolution = false
                  sendConfigurationRequest()
                }
              )
            }
          }
        }
      }
    }
  }

  // Handle cleanup
  DisposableEffect(Unit) {
    onDispose {
      releaseDecoder()
      // Make sure to exit fullscreen mode when disposing
      (context as? ViewerActivity)?.setFullscreenMode(false)
    }
  }
}