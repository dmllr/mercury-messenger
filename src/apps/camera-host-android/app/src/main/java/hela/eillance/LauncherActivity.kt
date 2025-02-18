package hela.eillance

import android.content.Intent
import android.content.SharedPreferences
import android.os.Bundle
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import hela.eillance.streamer.StreamerActivity
import hela.eillance.viewer.ViewerActivity

class LauncherActivity : AppCompatActivity() {

  private lateinit var sharedPrefs: SharedPreferences

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    sharedPrefs = getSharedPreferences("app_prefs", MODE_PRIVATE)

    // while in development, ask each time
    showRoleSelectionDialog()
//    when (sharedPrefs.getString("role", null)) {
//      null -> showRoleSelectionDialog() // First launch
//      "camera" -> startCameraActivity()
//      "controller" -> startControllerActivity()
//    }
  }

  private fun showRoleSelectionDialog() {
    AlertDialog.Builder(this)
      .setTitle("Select Mode")
      .setMessage("Choose how you want to use this device")
      .setPositiveButton("Camera Device") { _, _ ->
        saveRoleAndLaunch("camera")
      }
      .setNegativeButton("Controller Device") { _, _ ->
        saveRoleAndLaunch("controller")
      }
      .setCancelable(false)
      .show()
  }

  private fun saveRoleAndLaunch(role: String) {
    with(sharedPrefs.edit()) {
      putString("role", role)
      apply()
    }

    when (role) {
      "camera" -> startCameraActivity()
      "controller" -> startControllerActivity()
    }
    finish()
  }

  private fun startCameraActivity() {
    startActivity(Intent(this, StreamerActivity::class.java))
    finish()
  }

  private fun startControllerActivity() {
    startActivity(Intent(this, ViewerActivity::class.java))
    finish()
  }
}