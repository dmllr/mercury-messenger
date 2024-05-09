package me.hela.surveillance

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.graphics.SurfaceTexture
import android.os.Bundle
import android.os.IBinder
import android.util.Log
import android.view.Surface
import android.view.TextureView
import android.view.View
import androidx.core.app.ActivityCompat
import androidx.fragment.app.FragmentActivity
import me.hela.surveillance.camera.R
import me.hela.surveillance.camera.databinding.ActivityMainBinding


class MainActivity : FragmentActivity(), TextureView.SurfaceTextureListener, ServiceConnection {

  private var uiMode: Int = 0
  private var cameraService: CameraService? = null
  private var cameraServiceBound: Boolean = false

  private var previewSurface: Surface? = null
//  private var isPreview: Boolean = false

  private lateinit var binding: ActivityMainBinding


  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    // Initialize uiMode with the current UI mode
    uiMode = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK

    onWindowFocusChanged(true)

    binding = ActivityMainBinding.inflate(layoutInflater)
    setContentView(binding.root)

    setDefaults()
    setEventHandlers()

    checkAndRequestCamera()
  }

  override fun onConfigurationChanged(newConfig: Configuration) {
    super.onConfigurationChanged(newConfig)

    // Check if the theme has changed
    if (newConfig.uiMode and Configuration.UI_MODE_NIGHT_MASK != uiMode) {
      // Recreate the activity to apply the new theme
      recreate()
    }
  }

  private fun setDefaults() {
//    binding.btnTogglePreview.imageAlpha = 75
//    binding.btnTogglePreview.isEnabled = false
  }

  private fun setEventHandlers() {
    binding.btnPermissionWarning.setOnClickListener {
      checkAndRequestCamera()
    }

//    binding.btnTogglePreview.setOnClickListener {
//      isPreview = !isPreview
//      cameraService?.startPreview(isPreview)
//    }
  }

  override fun onDestroy() {
    unbindCameraService()

    super.onDestroy()
  }

  private fun pushFragment() {
    val fragment = CapturePreferencesFragment()

    supportFragmentManager.beginTransaction()
      .add(R.id.content_view, fragment)
      .commit()
  }

  private fun checkAndRequestCamera() {
    if (ActivityCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
      != PackageManager.PERMISSION_GRANTED
    ) {
      ActivityCompat.requestPermissions(
        this,
        arrayOf(Manifest.permission.CAMERA), PERMISSION_REQUEST_CODE_CAMERA
      )
      return
    }
    if (ActivityCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
      != PackageManager.PERMISSION_GRANTED
    ) {
      ActivityCompat.requestPermissions(
        this,
        arrayOf(Manifest.permission.RECORD_AUDIO), PERMISSION_REQUEST_CODE_RECORD_AUDIO
      )
      return
    }

    permissionGranted()

    startStreamingService()
    createTextureView()
  }

  private fun startStreamingService() {
    val cameraServiceIntent = Intent(applicationContext, CameraService::class.java)
    startService(cameraServiceIntent)
    if (applicationContext.bindService(
        cameraServiceIntent,
        this,
        Context.BIND_AUTO_CREATE + Context.BIND_ABOVE_CLIENT
      )
    )
      cameraServiceBound = true
  }

  private fun createTextureView() {
//    binding.texturePreview.surfaceTextureListener = this
//    if (binding.texturePreview.isAvailable) {
//      onSurfaceTextureAvailable(
//        binding.texturePreview.surfaceTexture!!,
//        binding.texturePreview.width, binding.texturePreview.height
//      )
//    }
  }

  override fun onRequestPermissionsResult(
    requestCode: Int,
    permissions: Array<out String>,
    grantResults: IntArray
  ) {
    if (requestCode != PERMISSION_REQUEST_CODE_CAMERA && requestCode != PERMISSION_REQUEST_CODE_RECORD_AUDIO) {
      super.onRequestPermissionsResult(requestCode, permissions, grantResults)
      return
    }

    if (grantResults.size == 1 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
      Thread {
        runOnUiThread {
          checkAndRequestCamera()
        }
      }.start()
    }
  }

  private fun permissionGranted() {
    binding.btnPermissionWarning.visibility = View.GONE
    pushFragment();
  }

  private fun unbindCameraService() {
    if (cameraServiceBound) {
      try {
        unbindService(this)
      } catch (_: IllegalArgumentException) {
        Log.e(Tune.TAG, "Trying to unbid service that is not registered")
      }
      cameraServiceBound = false
    }
  }

  // TextureView.SurfaceTextureListener implementation {
  override fun onSurfaceTextureAvailable(texture: SurfaceTexture, width: Int, height: Int) {
//    binding.btnTogglePreview.imageAlpha = 255
//    binding.btnTogglePreview.isEnabled = true

    val cameraPreviewSize = cameraService?.cameraPreviewSize ?: return

    texture.setDefaultBufferSize(cameraPreviewSize.width, cameraPreviewSize.height)

    val surface = Surface(texture)
    previewSurface = surface

    cameraService?.previewSurfaceCreated(surface)
  }

  override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
//    binding.btnTogglePreview.imageAlpha = 75
//    binding.btnTogglePreview.isEnabled = false

    cameraService?.previewSurfaceDestroyed(previewSurface!!)

    previewSurface = null
    return true
  }

  override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {}

  override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {}
  // TextureView.SurfaceTextureListener implementation }

  // ServiceConnection implementation {
  override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
    cameraService = (service as CameraService.StreamingServiceBinder).service
    createTextureView()
  }

  override fun onServiceDisconnected(name: ComponentName?) {
    cameraService = null
  }
  // ServiceConnection implementation }


  companion object {
    private const val PERMISSION_REQUEST_CODE_CAMERA: Int = 1
    private const val PERMISSION_REQUEST_CODE_RECORD_AUDIO: Int = 2
  }
}
