package hela.eillance.streamer

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.provider.Settings
import android.util.Log
import android.view.MenuItem
import android.view.View
import androidx.activity.OnBackPressedCallback
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.PopupMenu
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.fragment.app.FragmentManager
import hela.eillance.Tune
import hela.eillance.camera.R
import hela.eillance.camera.databinding.ActivityStreamerBinding


class StreamerActivity : AppCompatActivity(), ServiceConnection,
  FragmentManager.OnBackStackChangedListener {

  var streamerService: StreamerService? = null
  private var cameraServiceBound: Boolean = false
  private lateinit var binding: ActivityStreamerBinding

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    enableEdgeToEdge()
    binding = ActivityStreamerBinding.inflate(layoutInflater)
    setContentView(binding.root)
    ViewCompat.setOnApplyWindowInsetsListener(binding.main) { v, insets ->
      val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
      v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
      insets
    }

    setEventHandlers()
    checkAndRequestPermissions()

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

  private fun setEventHandlers() {
    binding.btnPermissionWarning.setOnClickListener {
      val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
        putExtra(Settings.EXTRA_APP_PACKAGE, packageName)
      }
      startActivity(intent)
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
                .replace(R.id.content, CapturePreferencesFragment.new(), fragmentTag)
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

  private fun pushOperationFragment() {
    supportFragmentManager.beginTransaction()
      .replace(R.id.content, OperationFragment.instance)
      .commit()
  }

  private fun checkAndRequestPermissions() {
    val permissionsToRequest = mutableListOf<String>()

    if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
      permissionsToRequest.add(Manifest.permission.CAMERA)
    }

    if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
      permissionsToRequest.add(Manifest.permission.RECORD_AUDIO)
    }

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
      checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
      permissionsToRequest.add(Manifest.permission.POST_NOTIFICATIONS)
    }

    if (permissionsToRequest.isNotEmpty()) {
      // Show rationale if needed
      if (shouldShowRationale(permissionsToRequest)) {
        showPermissionRationale(permissionsToRequest.toTypedArray())
      } else {
        requestPermissions(permissionsToRequest.toTypedArray(), PERMISSION_REQUEST_CODE)
      }
      return;
    }

    permissionGranted()
    startStreamingService()
  }

  private fun shouldShowRationale(permissions: List<String>): Boolean {
    return permissions.any { shouldShowRequestPermissionRationale(it) }
  }

  private fun showPermissionRationale(permissions: Array<String>) {
    AlertDialog.Builder(this)
      .setTitle(getString(R.string.permissions_required_rationale_title))
      .setMessage(getString(R.string.permissions_required_rationale_text))
      .setPositiveButton(getString(R.string.grant_permissions)) { _, _ ->
        requestPermissions(permissions, PERMISSION_REQUEST_CODE)
      }
      .setNegativeButton(getString(android.R.string.cancel)) { dialog, _ ->
        dialog.dismiss()
      }
      .setCancelable(false)
      .show()
  }

  private fun startStreamingService() {
    val streamerServiceIntent = Intent(applicationContext, StreamerService::class.java)
    startService(streamerServiceIntent)
    if (applicationContext.bindService(
        streamerServiceIntent,
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
    super.onRequestPermissionsResult(requestCode, permissions, grantResults)

    if (requestCode == PERMISSION_REQUEST_CODE) {
      if (grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
        permissionGranted()
        startStreamingService()
      } else {
        showSettingsRedirectDialog()
      }
    }
  }

  private fun showSettingsRedirectDialog() {
    AlertDialog.Builder(this)
      .setTitle("Permissions Required")
      .setMessage("You have denied necessary permissions. To enable streaming, please allow them in settings.")
      .setPositiveButton("Open Settings") { _, _ ->
        val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
          data = Uri.parse("package:$packageName")
        }
        startActivity(intent)
      }
      .setNegativeButton("Cancel") { dialog, _ ->
        dialog.dismiss()
      }
      .show()
  }

  private fun permissionGranted() {
    binding.btnPermissionWarning.visibility = View.GONE
    pushOperationFragment()
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
    streamerService = (service as StreamerService.StreamingServiceBinder).service
  }

  override fun onServiceDisconnected(name: ComponentName?) {
    streamerService = null
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
    private const val PERMISSION_REQUEST_CODE: Int = 10001
  }
}
