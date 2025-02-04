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
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.PopupMenu
import androidx.core.app.ActivityCompat
import androidx.fragment.app.FragmentManager
import hela.eillance.camera.R
import hela.eillance.camera.databinding.ActivityPerformerBinding
import hela.eillance.fragments.CapturePreferencesFragment
import hela.eillance.fragments.OperationFragment


class PerformerActivity : AppCompatActivity(), ServiceConnection,
  FragmentManager.OnBackStackChangedListener {

  var appEngineService: AppEngineService? = null
  private var cameraServiceBound: Boolean = false
  private lateinit var binding: ActivityPerformerBinding
  private var uiMode: Int = 0

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    uiMode = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK

    binding = ActivityPerformerBinding.inflate(layoutInflater)
    setContentView(binding.root)
    setEventHandlers()
    checkAndRequestCamera()

    val toolbar: androidx.appcompat.widget.Toolbar = findViewById(R.id.toolbar)
    setSupportActionBar(toolbar)

    supportFragmentManager.addOnBackStackChangedListener(this)
    updateActionBar()

    supportActionBar?.setHomeButtonEnabled(true)

    // Handle back button with OnBackPressedDispatcher
    onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
      override fun handleOnBackPressed() {
        if (supportFragmentManager.backStackEntryCount > 0) {
          supportFragmentManager.popBackStack()
        } else {
          finish() // or handle other back navigation logic
        }
      }
    })
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
    binding.icUserIcon.setOnClickListener {
      val popup = PopupMenu(this, it)
      popup.menuInflater.inflate(R.menu.user_menu, popup.menu)
      popup.setOnMenuItemClickListener { menuItem ->
        when (menuItem.itemId) {
          R.id.menu_item_preferences -> {
            val fragmentTag = CapturePreferencesFragment::class.java.simpleName
            val fragmentManager = supportFragmentManager

            // Check the fragment currently on top of the stack
            val currentFragment = fragmentManager.findFragmentById(R.id.content)

            // If the top fragment is already the one we want, do nothing
            if (currentFragment is CapturePreferencesFragment) {
              return@setOnMenuItemClickListener true
            }

            // If the fragment already exists in the stack, pop to it
            if (fragmentManager.findFragmentByTag(fragmentTag) != null) {
              fragmentManager.popBackStackImmediate(fragmentTag, 0)
            } else {
              // Otherwise, add the fragment and assign it a unique tag
              fragmentManager.beginTransaction()
                .replace(R.id.content, CapturePreferencesFragment.instance(), fragmentTag)
                .addToBackStack(fragmentTag)
                .commit()
            }
            true
          }

          else -> false  // return false if the click is not handled
        }
      }
      popup.show()
    }
  }

  override fun onDestroy() {
    unbindCameraService()
    supportFragmentManager.removeOnBackStackChangedListener(this)
    super.onDestroy()
  }

  override fun onOptionsItemSelected(item: MenuItem): Boolean {
    return when (item.itemId) {
      android.R.id.home -> {
        // Handle the back button event, popping the fragment back stack
        if (supportFragmentManager.backStackEntryCount > 0) {
          supportFragmentManager.popBackStack()
        } else {
          finish() // Handle default action
        }
        true
      }

      else -> super.onOptionsItemSelected(item)
    }
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
    val appEngineServiceIntent = Intent(applicationContext, AppEngineService::class.java)
    startService(appEngineServiceIntent)
    if (applicationContext.bindService(
        appEngineServiceIntent,
        this,
        Context.BIND_AUTO_CREATE + Context.BIND_ABOVE_CLIENT
      )
    ) {
      cameraServiceBound = true
    }
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
        Log.e(Tune.TAG, "Trying to unbind service that is not registered")
      }
      cameraServiceBound = false
    }
  }

  // ServiceConnection implementation
  override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
    appEngineService = (service as AppEngineService.StreamingServiceBinder).service
  }

  override fun onServiceDisconnected(name: ComponentName?) {
    appEngineService = null
  }
  // ServiceConnection implementation

  override fun onBackStackChanged() {
    updateActionBar()
  }

  override fun onSupportNavigateUp(): Boolean {
    onBackPressedDispatcher.onBackPressed() // Use OnBackPressedDispatcher instead
    return true
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
