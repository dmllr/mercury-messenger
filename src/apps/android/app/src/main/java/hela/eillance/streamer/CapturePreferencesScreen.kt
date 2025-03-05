package hela.eillance.streamer

import android.content.Context
import android.hardware.camera2.CameraManager
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.VideoConfig

@Composable
fun CapturePreferencesScreen(streamerService: StreamerService) {
  val context = LocalContext.current
  val activity = context as StreamerActivity
  val scrollState = rememberScrollState()

  Surface(modifier = Modifier.fillMaxSize()) {
    Column(
      modifier = Modifier
        .fillMaxSize()
        .verticalScroll(scrollState)
        .padding(16.dp)
    ) {
      var deviceName by remember { mutableStateOf("") }
      var videoConfig by remember { mutableStateOf(VideoConfig()) }
      var cameraIds by remember { mutableStateOf(emptyList<String>()) }
      var cameraNames by remember { mutableStateOf(emptyList<String>()) }
      var supportedResolutions by remember { mutableStateOf(emptyList<String>()) }

      // State for dialogs
      var showDeviceNameDialog by remember { mutableStateOf(false) }
      var showCameraDialog by remember { mutableStateOf(false) }
      var showResolutionDialog by remember { mutableStateOf(false) }
      var showFrameRateDialog by remember { mutableStateOf(false) }
      var showBitrateDialog by remember { mutableStateOf(false) }

      LaunchedEffect(Unit) {
        val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager

        // Get available cameras
        val availableCameraIds = CameraUtils.getCameraIds(cameraManager)
        cameraIds = availableCameraIds.toList()

        // Map camera IDs to names
        cameraNames = availableCameraIds.map {
          CameraUtils.getCameraName(cameraManager, it)
        }

        // Get supported resolutions for first camera
        val defaultCameraId = availableCameraIds.firstOrNull() ?: "0"
        val resolutions = CameraUtils.getSupportedResolutions(cameraManager, defaultCameraId)
        supportedResolutions = resolutions.map { it.toString() }
      }

      // Device Category
      PreferenceCategory(
        title = stringResource(R.string.preference_category_device),
        icon = painterResource(R.drawable.ic_pref_group_device)
      ) {
        EditTextPreference(
          title = stringResource(R.string.preference_device_name),
          icon = painterResource(R.drawable.ic_device_display_name),
          value = deviceName,
          summary = deviceName.ifEmpty { stringResource(android.R.string.untitled) },
          onClick = { showDeviceNameDialog = true }
        )

        // Show device name dialog when needed
        if (showDeviceNameDialog) {
          EditTextDialog(
            title = stringResource(R.string.preference_device_name),
            initialValue = deviceName,
            onDismiss = { showDeviceNameDialog = false },
            onConfirm = { newValue ->
              deviceName = newValue
              streamerService.configureDeviceName(newValue)
              showDeviceNameDialog = false
            }
          )
        }
      }

      // Video Capture Category
      PreferenceCategory(
        title = stringResource(R.string.preference_category_video_capture_encoding),
        icon = painterResource(R.drawable.ic_pref_group_video_encoding)
      ) {
        // Camera Selection
        ListPreference(
          title = stringResource(R.string.preference_default_camera),
          icon = painterResource(R.drawable.ic_pref_default_camera),
          summary = cameraNames.getOrNull(videoConfig.cameraId) ?: "Default Camera",
          onClick = { showCameraDialog = true }
        )

        if (showCameraDialog) {
          ListSelectionDialog(
            title = stringResource(R.string.preference_default_camera),
            entries = cameraNames,
            entryValues = cameraIds,
            selectedValue = videoConfig.cameraId.toString(),
            onDismiss = { showCameraDialog = false },
            onConfirm = { newValue ->
              val cameraId = newValue.toInt()
              videoConfig = videoConfig.copy(cameraId = cameraId)

              // Update supported resolutions
              val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
              val newResolutions = CameraUtils.getSupportedResolutions(cameraManager, newValue)
              supportedResolutions = newResolutions.map { it.toString() }

              streamerService.configureVideoCapture(videoConfig)
              showCameraDialog = false
            }
          )
        }

        // Camera Resolution
        ListPreference(
          title = stringResource(R.string.preference_camera_resolution),
          icon = painterResource(R.drawable.ic_pref_camera_resolution),
          summary = "${videoConfig.width}x${videoConfig.height}",
          onClick = { showResolutionDialog = true }
        )

        if (showResolutionDialog) {
          ListSelectionDialog(
            title = stringResource(R.string.preference_camera_resolution),
            entries = supportedResolutions,
            entryValues = supportedResolutions,
            selectedValue = "${videoConfig.width}x${videoConfig.height}",
            onDismiss = { showResolutionDialog = false },
            onConfirm = { newValue ->
              val resolution = newValue.split("x")
              if (resolution.size >= 2) {
                val width = resolution[0].toIntOrNull() ?: videoConfig.width
                val height = resolution[1].toIntOrNull() ?: videoConfig.height
                videoConfig = videoConfig.copy(width = width, height = height)
                streamerService.configureVideoCapture(videoConfig)
              }
              showResolutionDialog = false
            }
          )
        }

        // Frame Rate
        val frameRates = listOf("10", "15", "24", "25", "30", "60")
        ListPreference(
          title = stringResource(R.string.preference_frame_rate),
          icon = painterResource(R.drawable.ic_pref_frame_rate),
          summary = "${videoConfig.frameRate} fps",
          onClick = { showFrameRateDialog = true }
        )

        if (showFrameRateDialog) {
          ListSelectionDialog(
            title = stringResource(R.string.preference_frame_rate),
            entries = frameRates,
            entryValues = frameRates,
            selectedValue = videoConfig.frameRate.toString(),
            onDismiss = { showFrameRateDialog = false },
            onConfirm = { newValue ->
              val frameRate = newValue.toIntOrNull() ?: videoConfig.frameRate
              videoConfig = videoConfig.copy(frameRate = frameRate)
              streamerService.configureVideoCapture(videoConfig)
              showFrameRateDialog = false
            }
          )
        }

        // Bitrate
        val bitrates = listOf("Auto")
        ListPreference(
          title = stringResource(R.string.preference_bitrate),
          icon = painterResource(R.drawable.ic_pref_bitrate),
          summary = if (videoConfig.bitrate <= 0) "Auto" else "${videoConfig.bitrate} bps",
          onClick = { showBitrateDialog = true }
        )

        if (showBitrateDialog) {
          ListSelectionDialog(
            title = stringResource(R.string.preference_bitrate),
            entries = bitrates,
            entryValues = bitrates,
            selectedValue = if (videoConfig.bitrate <= 0) "Auto" else videoConfig.bitrate.toString(),
            onDismiss = { showBitrateDialog = false },
            onConfirm = { newValue ->
              val bitrate = if (newValue == "Auto") 0 else newValue.toIntOrNull() ?: 0
              videoConfig = videoConfig.copy(bitrate = bitrate)
              streamerService.configureVideoCapture(videoConfig)
              showBitrateDialog = false
            }
          )
        }
      }

      // Audio Capture Category
      PreferenceCategory(
        title = stringResource(R.string.preference_category_audio_capture_encoding),
        icon = painterResource(R.drawable.ic_pref_group_audio_encoding)
      ) {
        // Audio preferences would go here
      }
    }
  }
}

@Composable
fun PreferenceCategory(
  title: String,
  icon: androidx.compose.ui.graphics.painter.Painter,
  content: @Composable () -> Unit
) {
  Column(modifier = Modifier.padding(bottom = 16.dp)) {
    PreferenceCategoryHeader(title = title, icon = icon)
    content()
  }
}

@Composable
fun PreferenceCategoryHeader(
  title: String,
  icon: androidx.compose.ui.graphics.painter.Painter
) {
  ListItem(
    headlineContent = { Text(text = title) },
    leadingContent = {
      Icon(
        painter = icon,
        contentDescription = title
      )
    }
  )
}

@Composable
fun EditTextPreference(
  title: String,
  icon: androidx.compose.ui.graphics.painter.Painter,
  value: String,
  summary: String,
  onClick: () -> Unit
) {
  ListItem(
    headlineContent = { Text(text = title) },
    supportingContent = { Text(text = summary) },
    leadingContent = {
      Icon(
        painter = icon,
        contentDescription = title
      )
    },
    modifier = Modifier.clickable(onClick = onClick)
  )
}

@Composable
fun ListPreference(
  title: String,
  icon: androidx.compose.ui.graphics.painter.Painter,
  summary: String,
  onClick: () -> Unit
) {
  ListItem(
    headlineContent = { Text(text = title) },
    supportingContent = { Text(text = summary) },
    leadingContent = {
      Icon(
        painter = icon,
        contentDescription = title
      )
    },
    modifier = Modifier.clickable(onClick = onClick)
  )
}
