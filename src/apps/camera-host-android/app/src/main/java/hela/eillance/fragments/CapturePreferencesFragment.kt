package hela.eillance.fragments

import android.content.Context
import android.hardware.camera2.CameraManager
import android.os.Bundle
import android.util.TypedValue
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.preference.EditTextPreference
import androidx.preference.ListPreference
import androidx.preference.PreferenceFragmentCompat
import hela.eillance.MainActivity
import hela.eillance.camera.R
import hela.eillance.ndkio.VideoConfig
import hela.eillance.utils.CameraUtils
import kotlin.random.Random


class CapturePreferencesFragment : PreferenceFragmentCompat() {
  private var deviceName: String = ""
  private var videoConfig: VideoConfig = VideoConfig()

  companion object {
    fun instance() = CapturePreferencesFragment()
  }

  override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
    setPreferencesFromResource(R.xml.capture_preferences, rootKey)

    // Retrieve SharedPreferences
    val sharedPreferences = requireContext().getSharedPreferences("CapturePreferences", Context.MODE_PRIVATE)

    // Load deviceName from SharedPreferences if it exists
    deviceName = sharedPreferences.getString("device_name", "")!!;

    // Get the camera manager
    val cameraManager = requireContext().getSystemService(Context.CAMERA_SERVICE) as CameraManager

    // Get the list of available cameras
    val cameraIds = CameraUtils.getCameraIds(cameraManager)

    // Create the camera names and IDs arrays
    val cameraNames = arrayOfNulls<String>(cameraIds.size)
    val cameraIdsArray = arrayOfNulls<String>(cameraIds.size)
    for (i in cameraIds.indices) {
      cameraNames[i] = CameraUtils.getCameraName(cameraManager, cameraIds[i])
      cameraIdsArray[i] = cameraIds[i]
    }

    // Find the preferences
    val deviceNamePreference = findPreference<EditTextPreference>("device_name")
    val defaultCameraPreference = findPreference<ListPreference>("default_camera")
    val cameraResolutionPreference = findPreference<ListPreference>("camera_resolution")
    val frameRatePreference = findPreference<ListPreference>("frame_rate")
    val bitratePreference = findPreference<ListPreference>("bitrate")

    deviceName = deviceNamePreference?.text ?: deviceName
    videoConfig.cameraId = defaultCameraPreference?.value?.toInt() ?: videoConfig.cameraId
    val resolution = cameraResolutionPreference?.value?.split("x")
    videoConfig.width = resolution?.get(0)?.toInt() ?: videoConfig.width
    videoConfig.height = resolution?.get(1)?.toInt() ?: videoConfig.height
    videoConfig.frameRate = frameRatePreference?.value?.toInt() ?: videoConfig.frameRate
    videoConfig.bitrate = bitratePreference?.value?.toIntOrNull() ?: videoConfig.bitrate

    // Get the supported resolutions for the selected camera
    val supportedResolutions = CameraUtils.getSupportedResolutions(cameraManager,
      defaultCameraPreference?.value ?: cameraIds[0])

    // Create the camera resolutions array
    val cameraResolutions = arrayOfNulls<String>(supportedResolutions.size)
    for (i in supportedResolutions.indices) {
      cameraResolutions[i] = supportedResolutions[i].toString()
    }

    // Set the entries and entry values for the preferences
    defaultCameraPreference?.entries = cameraNames
    defaultCameraPreference?.entryValues = cameraIdsArray
    cameraResolutionPreference?.entries = cameraResolutions
    cameraResolutionPreference?.entryValues = cameraResolutions

    val defaultFrameRates = arrayOf("10", "15", "24", "25", "30", "60")
    frameRatePreference?.entries = defaultFrameRates
    frameRatePreference?.entryValues = defaultFrameRates

    val defaultBitrates = arrayOf("Auto")
    bitratePreference?.entries = defaultBitrates
    bitratePreference?.entryValues = defaultBitrates
    bitratePreference?.value = "Auto"

    // Set listeners
    deviceNamePreference?.setOnPreferenceChangeListener { _, value ->
      deviceName = value.toString()
      triggerDeviceNameUpdate()
      true
    }
    defaultCameraPreference?.setOnPreferenceChangeListener { _, value ->

      val newSupportedResolutions = CameraUtils.getSupportedResolutions(cameraManager, value as String)

      val newCameraResolutions = arrayOfNulls<String>(newSupportedResolutions.size)
      for (i in newSupportedResolutions.indices) {
        newCameraResolutions[i] = newSupportedResolutions[i].toString()
      }

      cameraResolutionPreference?.entries = newCameraResolutions
      cameraResolutionPreference?.entryValues = newCameraResolutions

      videoConfig.cameraId = value.toInt()
      triggerConfigurationUpdate()

      true
    }
    cameraResolutionPreference?.setOnPreferenceChangeListener { _, value ->
      val res = (value as String).split("x")
      videoConfig.width = res[0].toInt()
      videoConfig.height = res[1].toInt()
      triggerConfigurationUpdate()
      true
    }
    frameRatePreference?.setOnPreferenceChangeListener { _, value ->
      videoConfig.frameRate = (value as String).toInt()
      triggerConfigurationUpdate()
      true
    }
    bitratePreference?.setOnPreferenceChangeListener { _, value ->
      videoConfig.bitrate = (value as String).toIntOrNull() ?: 0
      triggerConfigurationUpdate()
      true
    }

    // Set the summary providers for the preferences
    deviceNamePreference?.summaryProvider = EditTextPreference.SimpleSummaryProvider.getInstance()
    defaultCameraPreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
    cameraResolutionPreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
    frameRatePreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
    bitratePreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
  }

  private fun triggerDeviceNameUpdate() {
    (requireActivity() as MainActivity).appEngineService?.configureDeviceName(deviceName)
  }

  private fun triggerConfigurationUpdate() {
    (requireActivity() as MainActivity).appEngineService?.configureVideoCapture(videoConfig)
  }

  override fun onCreateView(
    inflater: LayoutInflater,
    container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    // replace transparent background with the default theme color background
    val view = super.onCreateView(inflater, container, savedInstanceState)
    val bgColorValue = TypedValue()
    requireActivity().theme.resolveAttribute(android.R.attr.windowBackground, bgColorValue, true)
    view.setBackgroundResource(bgColorValue.resourceId)
    return view
  }
}
