package hela.eillance.viewer

import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
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
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
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
  val status by viewModel.peerStatus.observeAsState()
  val resolution by viewModel.streamResolution.observeAsState()
  val profile by viewModel.deviceProfile.observeAsState()

  var peerConnectionState by remember { mutableStateOf(ServiceConnectionState.Unknown) }
  var deviceProfile by remember { mutableStateOf(peer.profile) }
  var statusText by remember { mutableStateOf("") }

  // Camera selection state
  var cameraOptions by remember { mutableStateOf(listOf("Default Camera")) }
  var selectedCameraIndex by remember { mutableIntStateOf(0) }
  var expandedCamera by remember { mutableStateOf(false) }

  // Resolution selection state
  var resolutionOptions by remember { mutableStateOf(listOf("Default Resolution")) }
  var selectedResolutionIndex by remember { mutableIntStateOf(0) }
  var expandedResolution by remember { mutableStateOf(false) }

  // Surface dimensions
  var surfaceWidth by remember { mutableIntStateOf(0) }
  var surfaceHeight by remember { mutableIntStateOf(250) }

  // Update camera options when profile changes
  fun updateCameraOptions(profile: Profile) {
    val names = profile.map { it.name }
    cameraOptions = names.ifEmpty { listOf("Default Camera") }
  }

  // Update resolution options when camera selection changes
  fun updateResolutionOptions(cameraIndex: Int) {
    if (cameraIndex in deviceProfile.indices) {
      val resolutions = deviceProfile[cameraIndex].resolutions
      resolutionOptions = resolutions.map { "${it.width}$RESOLUTION_SEPARATOR${it.height}" }
    } else {
      resolutionOptions = listOf("Default Resolution")
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
  LaunchedEffect(key1 = status) {
    status?.let { (source, statusCode) ->
      if (source == EventSource.PeerConnectionState) {
        peerConnectionState = ServiceConnectionState.fromInt(statusCode)
      }
      statusText = "${source.name}: $statusCode"
    }
  }

  // Handle resolution changes
  LaunchedEffect(key1 = resolution) {
    resolution?.let { (width, height) ->
      if (width > 0 && height > 0 && surfaceWidth > 0) {
        surfaceHeight = (surfaceWidth * height) / width
      }
    }
  }

  // Handle profile updates
  LaunchedEffect(key1 = profile) {
    profile?.let {
      deviceProfile = it
      updateCameraOptions(it)

      // If there are cameras available, select the first one automatically
      if (it.isNotEmpty()) {
        selectedCameraIndex = 0
        updateResolutionOptions(0) // Set resolutions for the first camera
      }
    }
  }

  // Initial stream request on composition
  LaunchedEffect(key1 = true) {
    (context as? ViewerActivity)?.sendStreamRequest(peer.id, composeConfigurationMessage())
  }

  ConstraintLayout(
    modifier = Modifier.fillMaxSize()
  ) {
    val (surface, cameraSpinner, resolutionSpinner, rotateButton, statusTextRef) = createRefs()

    val density = LocalDensity.current.density

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
              // Not used
            }
          })
        }
      },
      modifier = Modifier
        .fillMaxWidth()
        .height(surfaceHeight.dp)
        .onGloballyPositioned { layoutCoordinates ->
          if (surfaceWidth == 0) { // Only update once to prevent unnecessary recomputations
            surfaceWidth = (layoutCoordinates.size.width / density).toInt()
          }
        }
        .constrainAs(surface) {
          top.linkTo(parent.top)
          start.linkTo(parent.start)
          end.linkTo(parent.end)
        }
    )

    // Camera selection dropdown
    ExposedDropdownMenuBox(
      expanded = expandedCamera,
      onExpandedChange = { expandedCamera = it },
      modifier = Modifier
        .constrainAs(cameraSpinner) {
          top.linkTo(surface.bottom)
          start.linkTo(parent.start)
        }
    ) {
      TextField(
        value = cameraOptions.getOrElse(selectedCameraIndex) { "Default Camera" },
        onValueChange = {},
        readOnly = true,
        trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expandedCamera) },
        colors = ExposedDropdownMenuDefaults.textFieldColors(),
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
      onExpandedChange = { expandedResolution = it },
      modifier = Modifier
        .constrainAs(resolutionSpinner) {
          top.linkTo(surface.bottom)
          start.linkTo(cameraSpinner.end, 8.dp)
        }
    ) {
      TextField(
        value = resolutionOptions.getOrElse(selectedResolutionIndex) { "Default Resolution" },
        onValueChange = {},
        readOnly = true,
        trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expandedResolution) },
        colors = ExposedDropdownMenuDefaults.textFieldColors(),
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

    // Rotate button
    IconButton(
      onClick = { /* Implement rotation logic if needed */ },
      modifier = Modifier
        .constrainAs(rotateButton) {
          bottom.linkTo(surface.bottom)
          end.linkTo(parent.end)
        }
    ) {
      Icon(
        painter = painterResource(id = R.drawable.ic_surface_rotate),
        contentDescription = stringResource(id = R.string.fr_observer_operation_change_video_orientation)
      )
    }

    // Status text
    Text(
      text = statusText,
      modifier = Modifier
        .constrainAs(statusTextRef) {
          top.linkTo(surface.top)
          bottom.linkTo(surface.bottom)
          start.linkTo(surface.start)
          end.linkTo(surface.end)
        }
    )
  }

  // Handle cleanup
  DisposableEffect(key1 = Unit) {
    onDispose {
      releaseDecoder()
    }
  }
}
