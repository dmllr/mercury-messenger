package hela.eillance

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.Bundle
import android.os.IBinder
import android.util.Log
import android.view.MenuItem
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.fragment.app.FragmentManager
import hela.eillance.camera.R
import hela.eillance.camera.databinding.ActivityMainBinding


class MainActivity : AppCompatActivity(), ServiceConnection, FragmentManager.OnBackStackChangedListener {

  var cameraService: CameraService? = null
  private var cameraServiceBound: Boolean = false
  private lateinit var binding: ActivityMainBinding

  private var uiMode: Int = 0

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    uiMode = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK

    onWindowFocusChanged(true)

    binding = ActivityMainBinding.inflate(layoutInflater)
    setContentView(binding.root)
    setEventHandlers()
    checkAndRequestCamera()

    supportFragmentManager.addOnBackStackChangedListener(this)
    updateActionBar()

    supportActionBar?.setHomeButtonEnabled(true)
  }

  override fun onConfigurationChanged(newConfig: Configuration) {
    super.onConfigurationChanged(newConfig)

    // Check if the theme has changed
    if (newConfig.uiMode and Configuration.UI_MODE_NIGHT_MASK != uiMode) {
      // Recreate the activity to apply the new theme
      recreate()
    }
  }

  private fun setEventHandlers() {
    binding.btnPermissionWarning.setOnClickListener {
      checkAndRequestCamera()
    }
  }

  override fun onDestroy() {
    unbindCameraService()
    supportFragmentManager.removeOnBackStackChangedListener(this)
    super.onDestroy()
  }

  override fun onOptionsItemSelected(item: MenuItem): Boolean {
    when (item.itemId) {
      android.R.id.home -> {
        onBackPressedDispatcher.onBackPressed()
        return true
      }
    }
    return super.onOptionsItemSelected(item)
  }

  private fun pushFragment() {
    supportFragmentManager.beginTransaction()
      .replace(R.id.content, OperationFragment.instance())
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
    pushFragment()
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

  // ServiceConnection implementation
  override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
    cameraService = (service as CameraService.StreamingServiceBinder).service
  }

  override fun onServiceDisconnected(name: ComponentName?) {
    cameraService = null
  }
  // ServiceConnection implementation

  override fun onBackStackChanged() {
    updateActionBar()
  }

  private fun updateActionBar() {
    val backStackCount = supportFragmentManager.backStackEntryCount
    supportActionBar?.setDisplayHomeAsUpEnabled(backStackCount > 0)
    if (backStackCount > 0) {
      // Show back button, hide hamburger menu
      supportActionBar?.setHomeAsUpIndicator(null)
    } else {
      // Show hamburger menu, hide back button
      supportActionBar?.setHomeAsUpIndicator(R.drawable.ic_status2)
    }
  }

  companion object {
    private const val PERMISSION_REQUEST_CODE_CAMERA: Int = 1
    private const val PERMISSION_REQUEST_CODE_RECORD_AUDIO: Int = 2
  }
}
