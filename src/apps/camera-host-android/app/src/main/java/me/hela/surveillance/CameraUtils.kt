package me.hela.surveillance

import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.util.Size
import java.util.Arrays

object CameraUtils {
  fun getCameraIds(cameraManager: CameraManager): List<String> {
    try {
      return listOf(*cameraManager.cameraIdList)
    } catch (e: CameraAccessException) {
      e.printStackTrace()
      return emptyList()
    }
  }

  fun getSupportedResolutions(cameraManager: CameraManager, cameraId: String?): List<Size> {
    try {
      val characteristics = cameraManager.getCameraCharacteristics(cameraId!!)
      val streamConfigurationMap =
        characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)!!
      return listOf(
        *streamConfigurationMap.getOutputSizes(
          SurfaceTexture::class.java
        )
      )
    } catch (e: CameraAccessException) {
      e.printStackTrace()
      return emptyList()
    }
  }
}