package hela.eillance.viewer

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.pm.ActivityInfo
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
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
import androidx.compose.ui.graphics.Color
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

@Composable
fun LockScreenOrientation(orientation: Int) {
  val context = LocalContext.current
  DisposableEffect(orientation) {
    val activity = context.findActivity() ?: return@DisposableEffect onDispose {}
    val originalOrientation = activity.requestedOrientation
    activity.requestedOrientation = orientation
    onDispose {
      // restore original orientation when view disappears
      activity.requestedOrientation = originalOrientation
    }
  }
}

fun Context.findActivity(): Activity? = when (this) {
  is Activity -> this
  is ContextWrapper -> baseContext.findActivity()
  else -> null
}

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

  // Surface dimensions
  var surfaceLongEdge by remember { mutableIntStateOf(0) }
  var surfaceShortEdge by remember { mutableIntStateOf(250) }

  var orientation by remember { mutableIntStateOf(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT) }
  LockScreenOrientation(orientation)

  // Determine if we're in portrait mode
  val isPortrait = orientation == ActivityInfo.SCREEN_ORIENTATION_PORTRAIT

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

  // Handle resolution changes
  LaunchedEffect(resolution, orientation) {
    resolution?.let { (width, height) ->
      if (width > 0 && height > 0) {
        if (isPortrait) {
          surfaceShortEdge = (surfaceLongEdge * height) / width
        } else {
          surfaceLongEdge = (surfaceShortEdge * width) / height
        }
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

  ConstraintLayout(
    modifier = Modifier.fillMaxSize()
  ) {
    val (surface, cameraSpinner, resolutionSpinner, rotateButton, statusTextRef) = createRefs()

    val density = LocalDensity.current.density

    // Surface view for video output with orientation-dependent modifiers
    val surfaceModifier = if (isPortrait) {
      Modifier
        .fillMaxWidth()
        .height(surfaceShortEdge.dp)
        .onGloballyPositioned { layoutCoordinates ->
          if (surfaceLongEdge == 0) {
            surfaceLongEdge = (layoutCoordinates.size.width / density).toInt()
          }
        }
        .constrainAs(surface) {
          top.linkTo(parent.top)
          start.linkTo(parent.start)
          end.linkTo(parent.end)
        }
    } else {
      Modifier
        .fillMaxHeight()
        .width(surfaceLongEdge.dp)
        .onGloballyPositioned { layoutCoordinates ->
          surfaceShortEdge = (layoutCoordinates.size.height / density).toInt()
        }
        .constrainAs(surface) {
          top.linkTo(parent.top)
          bottom.linkTo(parent.bottom)
          start.linkTo(parent.start)
          end.linkTo(parent.end)
        }
    }

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
      modifier = surfaceModifier
    )

    val transparentTextFieldColors = ExposedDropdownMenuDefaults.textFieldColors(
      unfocusedIndicatorColor = Color.Transparent,
      focusedIndicatorColor = Color.Transparent,
      unfocusedContainerColor = Color.Transparent,
      focusedContainerColor = Color.Transparent
    )

    // Camera selection dropdown
    ExposedDropdownMenuBox(
      expanded = expandedCamera,
      onExpandedChange = { expandedCamera = it },
      modifier = Modifier
        .constrainAs(cameraSpinner) {
          bottom.linkTo(surface.bottom)
          start.linkTo(parent.start)
        }
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
      onExpandedChange = { expandedResolution = it },
      modifier = Modifier
        .constrainAs(resolutionSpinner) {
          bottom.linkTo(surface.bottom)
          start.linkTo(cameraSpinner.end, 8.dp)
        }
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

    // Rotate button
    IconButton(
      onClick = {
        orientation = if (orientation == ActivityInfo.SCREEN_ORIENTATION_PORTRAIT) {
          ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        } else {
          ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        }
      },
      modifier = Modifier
        .constrainAs(rotateButton) {
          top.linkTo(surface.top)
          start.linkTo(surface.start)
        }
    ) {
      Icon(
        painter = painterResource(id = R.drawable.ic_surface_rotate),
        contentDescription = stringResource(id = R.string.viewer_StreamScreen_change_video_orientation)
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
  DisposableEffect(Unit) {
    onDispose {
      releaseDecoder()
    }
  }
}
