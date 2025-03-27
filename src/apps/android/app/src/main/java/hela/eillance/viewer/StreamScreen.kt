package hela.eillance.viewer

import android.content.res.Configuration
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.view.Surface
import android.view.TextureView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.res.vectorResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.constraintlayout.compose.ConstraintLayout
import androidx.lifecycle.viewmodel.compose.viewModel
import hela.eillance.camera.R
import hela.eillance.ndkio.Config
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.Peer
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.ndkio.PeerStateIce
import hela.eillance.ndkio.PeerStateIceGathering
import hela.eillance.ndkio.PeerStateSignaling
import hela.eillance.ndkio.Profile
import kotlinx.coroutines.delay

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
  val pingEvent by viewModel.pingEvent.observeAsState()
  val streamInfo by viewModel.streamInfo.observeAsState()
  val profile by viewModel.deviceProfile.observeAsState()

  var peerConnectionState by remember { mutableStateOf(PeerConnectionState.Unknown) }
  var deviceProfile by remember { mutableStateOf(peer.profile) }
  var statusText by remember { mutableStateOf("") }
  val statusScrollState = rememberScrollState()

  // Flag to control status visibility
  var showStatus by remember { mutableStateOf(true) }

  // Camera selection state
  var cameraOptions by remember { mutableStateOf(listOf(defaultCameraLabel)) }
  var selectedCameraIndex by remember { mutableIntStateOf(0) }
  var expandedCamera by remember { mutableStateOf(false) }

  // Resolution selection state
  var resolutionOptions by remember { mutableStateOf(listOf(defaultResolutionsLabel)) }
  var selectedResolutionIndex by remember { mutableIntStateOf(0) }
  var expandedResolution by remember { mutableStateOf(false) }

  // Add rotation state (0, 90, 180, 270 degrees)
  var localRotation by remember { mutableIntStateOf(0) }

  var textureView by remember { mutableStateOf<TextureView?>(null) }
  var currentScale by remember { mutableFloatStateOf(1f) }

  // Fullscreen state - starts as true by default
  var isFullscreen by remember { mutableStateOf(true) }

  // Ping indicator state
  var pingColor by remember { mutableStateOf(Color.Transparent) }

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

  // Decode status code to human-readable string based on the source
  fun decodeStatusCode(source: EventSource, statusCode: Int): String {
    return when (source) {
      EventSource.PeerConnectionState -> PeerConnectionState.fromInt(statusCode).getResourceString(context)
      EventSource.IceState -> PeerStateIce.fromInt(statusCode).getResourceString(context)
      EventSource.GatheringState -> PeerStateIceGathering.fromInt(statusCode).getResourceString(context)
      EventSource.SignalingState -> PeerStateSignaling.fromInt(statusCode).getResourceString(context)
      else -> ""
    }
  }

  // Send configuration request
  fun sendConfigurationRequest() {
    if (peerConnectionState == PeerConnectionState.Connected) {
      (context as? ViewerActivity)?.sendConfigurationRequest(peer.id, composeConfigurationMessage())
    }
  }

  // Calculate transform matrix based on device and video orientations
  fun calculateTransformMatrix(scale: Float = 1f): Matrix {
    // Incoming video default is landscape, top to left, this means rotation 0
    //    rotation: 0
    //    /---------------------\
    //    |                     |   +-----> stream width, physical width
    //    | o   <-- top camera  |   |
    //    |                     |   v stream height, physical height
    //    \---------------------/
    //
    //    rotation: 90
    //    /-------------\
    //    |      o      |           +-----> stream height, physical width
    //    |      ^      |           |
    //    |     top     |           v stream width, physical height
    //    |             |
    //    |             |
    //    |             |
    //    \-------------/
    // viewModel.streamRotation specifies angle to rotate the video by

    val matrix = Matrix()

    val view = textureView ?: return matrix

    // Stream details
    val streamRotation = viewModel.streamRotation + localRotation
    val isStreamPortrait = streamRotation == 90 || streamRotation == 270
    var streamWidth = viewModel.streamWidth.toFloat()
    var streamHeight = viewModel.streamHeight.toFloat()

    // View details
    val isViewPortrait =
      (context.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT)
    val viewWidth = view.width.toFloat()
    val viewHeight = view.height.toFloat()

    if (streamWidth == 0f || streamHeight == 0f || viewWidth == 0f || viewHeight == 0f) {
      return matrix
    }

    // Center of the view. Perform all operations on this point.
    val pivotX = viewWidth / 2f
    val pivotY = viewHeight / 2f

    matrix.postRotate(streamRotation.toFloat(), pivotX, pivotY)

    // The scale to maintain aspect ratio
    val scaleAspectX = streamHeight / streamWidth
    val scaleAspectY = streamWidth / streamHeight

    when {
      // Stream portrait, view portrait
      isStreamPortrait && isViewPortrait -> {
        // first, maintain aspect ratio
        matrix.postScale(scaleAspectX, scaleAspectY, pivotX, pivotY)
        // next, fit the view
        val scaleToFit = viewWidth / streamWidth
        matrix.postScale(scaleToFit, scaleToFit, pivotX, pivotY)
      }
      // Both landscape
      !isStreamPortrait && !isViewPortrait -> {
        // just maintain aspect ratio
        matrix.postScale(scaleAspectX, 1f, pivotX, pivotY)
      }
      // Stream portrait, view landscape
      isStreamPortrait && !isViewPortrait -> {
        val scaleToFit = viewHeight / streamWidth
        matrix.postScale(scaleToFit, scaleToFit, pivotX, pivotY)
      }
      // Stream landscape, view portrait
      else -> { // !isStreamPortrait && isViewPortrait
        val scaleToFit = viewWidth / streamWidth
        matrix.postScale(scaleAspectY * scaleToFit, scaleAspectX * scaleToFit, pivotX, pivotY)
      }
    }

    // Apply user scale if greater than 1
    if (scale > 1f) {
      matrix.postScale(scale, scale, pivotX, pivotY)
    }

    return matrix
  }

  // Rotate video by 90 degrees
  fun rotateVideo() {
    localRotation = (localRotation + 90) % 360
    textureView?.setTransform(calculateTransformMatrix())
  }

  val (scale, _, gestureModifier) = rememberTextureViewScaling(
    textureView = textureView,
    calculateBaseMatrix = { calculateTransformMatrix() }
  )

  // Update currentScale when scale changes
  LaunchedEffect(scale) {
    currentScale = scale
  }

  // Handle status updates with improved readability
  LaunchedEffect(status) {
    status?.let { (source, statusCode) ->
      val decodedStatus = decodeStatusCode(source, statusCode)

      // Special handling for PeerConnectionState
      if (source == EventSource.PeerConnectionState) {
        peerConnectionState = PeerConnectionState.fromInt(statusCode)
        statusText += "${source.name}: ${peerConnectionState.getResourceString(context)}\n"

        // Hide status when connection is Online, show otherwise
        showStatus = peerConnectionState != PeerConnectionState.Connected
      } else if (source != EventSource.Ping) { // Skip ping events as they're handled separately
        if (decodedStatus.isNotEmpty()) {
          statusText += "${source.name}: $decodedStatus\n"
        }
      }
    }
  }

  // Handle ping events with dedicated LiveData
  LaunchedEffect(pingEvent) {
    pingEvent?.let {
      // Flash the ping indicator
      pingColor = Color.Green
      delay(333)
      pingColor = Color.Transparent
    }
  }

  // Handle resolution updates
  LaunchedEffect(streamInfo) {
    streamInfo?.let { (_, _, _) ->
      textureView?.setTransform(calculateTransformMatrix(currentScale))
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
    var (videoSurface, controlsOverlay) = createRefs()

    // TextureView for video output
    AndroidView(
      factory = { ctx ->
        TextureView(ctx).apply {
          textureView = this
          surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
              // Initialize decoder with a Surface created from the SurfaceTexture
              initDecoder(Surface(surface))
            }

            override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
              // Update transform when size changes
              setTransform(calculateTransformMatrix())
            }

            override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
              releaseDecoder()
              return true
            }

            override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
              // This method is called frequently when frames are rendered
              // No need to do anything here for our use case
            }
          }
        }
      },
      modifier = Modifier
        .constrainAs(videoSurface) {
          top.linkTo(parent.top)
          bottom.linkTo(parent.bottom)
          start.linkTo(parent.start)
          end.linkTo(parent.end)
        }
        .background(Color.Black)
        .fillMaxSize()
        .then(gestureModifier)
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
        focusedContainerColor = Color.Transparent,
        unfocusedTextColor = Color.White,
        focusedTextColor = Color.White
      )

      // Add scale indicator when zoomed
      if (currentScale > 1.01f) {
        Text(
          text = "${(currentScale * 100).toInt()}%",
          color = Color.White,
          modifier = Modifier
            .padding(16.dp)
            .background(
              color = Color.Black.copy(alpha = 0.3f),
              shape = RoundedCornerShape(4.dp)
            )
            .padding(horizontal = 8.dp, vertical = 4.dp)
            .align(Alignment.TopCenter)
        )
      }

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

      // Ping indicator in top-right corner
      Text(
        text = stringResource(id = R.string.led),
        color = pingColor,
        modifier = Modifier
          .padding(16.dp)
          .padding(horizontal = 8.dp, vertical = 4.dp)
          .align(Alignment.TopEnd)
      )

      // Scrollable status text in center, only visible when not connected
      if (statusText.isNotEmpty() && showStatus) {
        Box(
          modifier = Modifier
            .background(Color.Black.copy(alpha = 0.5f), shape = RoundedCornerShape(4.dp))
            .padding(8.dp)
            .heightIn(max = 200.dp)
            .align(Alignment.Center)
        ) {
          Text(
            text = statusText,
            color = Color.White,
            modifier = Modifier
              .verticalScroll(statusScrollState)
          )
        }
      }

      // Control elements at bottom
      Row(
        modifier = Modifier
          .fillMaxWidth()
          .heightIn(min = 56.dp)
          .background(Color.Black.copy(alpha = 0.5f))
          .padding(horizontal = 8.dp, vertical = 12.dp)
          .align(Alignment.BottomCenter),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically
      ) {
        // Updated Camera selection dropdown
        ExposedDropdownMenuBox(
          expanded = expandedCamera,
          onExpandedChange = { expandedCamera = it },
          modifier = Modifier
            .weight(1f)
        ) {
          TextField(
            value = cameraOptions.getOrElse(selectedCameraIndex) { "Default Camera" },
            onValueChange = {},
            readOnly = true,
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expandedCamera) },
            colors = transparentTextFieldColors,
            modifier = Modifier
              .menuAnchor(MenuAnchorType.PrimaryNotEditable, true)
          )

          ExposedDropdownMenu(
            expanded = expandedCamera,
            onDismissRequest = { expandedCamera = false },
            modifier = Modifier
              .background(Color.DarkGray.copy(alpha = 0.9f))
          ) {
            cameraOptions.forEachIndexed { index, option ->
              androidx.compose.material3.DropdownMenuItem(
                text = { Text(text = option, color = Color.White) },
                onClick = {
                  selectedCameraIndex = index
                  expandedCamera = false
                  updateResolutionOptions(index)
                  sendConfigurationRequest()
                },
                contentPadding = androidx.compose.material3.MenuDefaults.DropdownMenuItemContentPadding
              )
            }
          }
        }

        // Updated Resolution selection dropdown
        ExposedDropdownMenuBox(
          expanded = expandedResolution,
          onExpandedChange = { expandedResolution = it },
          modifier = Modifier
            .weight(1f)
        ) {
          TextField(
            value = resolutionOptions.getOrElse(selectedResolutionIndex) { "Default Resolution" },
            onValueChange = {},
            readOnly = true,
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expandedResolution) },
            colors = transparentTextFieldColors,
            modifier = Modifier
              .menuAnchor(MenuAnchorType.PrimaryNotEditable, true)
          )

          ExposedDropdownMenu(
            expanded = expandedResolution,
            onDismissRequest = { expandedResolution = false },
            modifier = Modifier
              .background(Color.DarkGray.copy(alpha = 0.9f))
          ) {
            resolutionOptions.forEachIndexed { index, option ->
              androidx.compose.material3.DropdownMenuItem(
                text = { Text(text = option, color = Color.White) },
                onClick = {
                  selectedResolutionIndex = index
                  expandedResolution = false
                  sendConfigurationRequest()
                },
                contentPadding = androidx.compose.material3.MenuDefaults.DropdownMenuItemContentPadding
              )
            }
          }
        }

        // Rotation Button
        IconButton(
          onClick = { rotateVideo() },
          modifier = Modifier
            .size(47.dp)
            .aspectRatio(1f)
        ) {
          Icon(
            imageVector = ImageVector.vectorResource(R.drawable.ic_surface_rotate),
            contentDescription = "Rotate Video",
            tint = Color.White
          )
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
