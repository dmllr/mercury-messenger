package me.hela.surveillance

import android.content.Context
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.os.Bundle
import androidx.preference.ListPreference
import androidx.preference.PreferenceFragmentCompat
import me.hela.surveillance.camera.R

class CapturePreferencesFragment : PreferenceFragmentCompat() {
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

    // Set a listener to update the camera resolution preference when the default camera is changed
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

    // Set the summary providers for the preferences
    defaultCameraPreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
    cameraResolutionPreference?.summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
  }
}
