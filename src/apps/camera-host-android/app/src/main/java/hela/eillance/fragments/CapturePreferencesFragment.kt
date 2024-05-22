package hela.eillance.fragments

import android.content.Context
import android.hardware.camera2.CameraManager
import android.os.Bundle
import android.util.TypedValue
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.preference.ListPreference
import androidx.preference.PreferenceFragmentCompat
import hela.eillance.camera.R
import hela.eillance.utils.CameraUtils


class CapturePreferencesFragment : PreferenceFragmentCompat() {
  companion object {
    fun instance() = CapturePreferencesFragment()
  }

  override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
    setPreferencesFromResource(R.xml.capture_preferences, rootKey)

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

    // Find the default camera and camera resolution preferences
    val defaultCameraPreference = findPreference<ListPreference>("default_camera")
    val cameraResolutionPreference = findPreference<ListPreference>("camera_resolution")
    val frameRatePreference = findPreference<ListPreference>("frame_rate")
    val bitratePreference = findPreference<ListPreference>("bitrate")

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
    defaultCameraPreference?.setOnPreferenceChangeListener { _, newValue ->
      val newCameraId = newValue as String
      val newSupportedResolutions = CameraUtils.getSupportedResolutions(cameraManager, newCameraId)

      val newCameraResolutions = arrayOfNulls<String>(newSupportedResolutions.size)
      for (i in newSupportedResolutions.indices) {
        newCameraResolutions[i] = newSupportedResolutions[i].toString()
      }

      cameraResolutionPreference?.entries = newCameraResolutions
      cameraResolutionPreference?.entryValues = newCameraResolutions

      true
    }
    cameraResolutionPreference?.setOnPreferenceChangeListener { _, newValue ->
      true
    }
    frameRatePreference?.setOnPreferenceChangeListener { _, newValue ->
      true
    }
    bitratePreference?.setOnPreferenceChangeListener { _, newValue ->
      true
    }

    // Set the summary providers for the preferences
    defaultCameraPreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
    cameraResolutionPreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
    frameRatePreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
    bitratePreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
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
