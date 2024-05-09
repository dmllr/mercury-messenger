package me.hela.surveillance

import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.media.MediaRecorder
import android.util.Size

object CameraUtils {
  fun getCameraIds(cameraManager: CameraManager): List<String> {
    try {
      return listOf(*cameraManager.cameraIdList)
    } catch (e: CameraAccessException) {
      e.printStackTrace()
      return emptyList()
    }
  }

  fun getCameraName(cameraManager: CameraManager, cameraId: String): String {
    try {
      val characteristics = cameraManager.getCameraCharacteristics(cameraId)
      val facing = characteristics.get(CameraCharacteristics.LENS_FACING)
      return when (facing) {
        CameraCharacteristics.LENS_FACING_FRONT -> "Front Camera"
        CameraCharacteristics.LENS_FACING_BACK -> "Back Camera"
        CameraCharacteristics.LENS_FACING_EXTERNAL -> "External Camera"
        else -> "Unknown Camera"
      }
    } catch (e: CameraAccessException) {
      e.printStackTrace()
      return "Unknown Camera"
    }
  }

  fun getSupportedResolutions(cameraManager: CameraManager, cameraId: String?): List<Size> {
    try {
      val characteristics = cameraManager.getCameraCharacteristics(cameraId!!)
      val streamConfigurationMap = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)!!

      val videoResolutions = streamConfigurationMap.getOutputSizes(MediaRecorder::class.java)
      return listOf(
        *videoResolutions
      )
    } catch (e: CameraAccessException) {
      e.printStackTrace()
      return emptyList()
    }
  }
}