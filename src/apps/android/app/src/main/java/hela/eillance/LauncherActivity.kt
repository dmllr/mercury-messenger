package hela.eillance

import android.content.Intent
import android.content.SharedPreferences
import android.os.Bundle
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.firebase.ui.auth.AuthUI
import com.firebase.ui.auth.FirebaseAuthUIActivityResultContract
import com.firebase.ui.auth.data.model.FirebaseAuthUIAuthenticationResult
import com.google.firebase.auth.FirebaseAuth
import hela.eillance.camera.R
import hela.eillance.streamer.StreamerActivity
import hela.eillance.viewer.ViewerActivity

class LauncherActivity : AppCompatActivity() {

  private val signInLauncher = registerForActivityResult(
    FirebaseAuthUIActivityResultContract(),
  ) { res ->
    this.onSignInResult(res)
  }

  // authentication providers
  private val providers = arrayListOf(
    AuthUI.IdpConfig.EmailBuilder().build(),
    AuthUI.IdpConfig.PhoneBuilder().build(),
    AuthUI.IdpConfig.GoogleBuilder().build(),
  )

  private lateinit var sharedPrefs: SharedPreferences

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    sharedPrefs = getSharedPreferences("app_prefs", MODE_PRIVATE)

    val auth = FirebaseAuth.getInstance()
    if (auth.currentUser != null) {
      // already signed in
      followSelectedRole()
    } else {
      val signInIntent = AuthUI.getInstance()
        .createSignInIntentBuilder()
        .setAvailableProviders(providers)
        .setLogo(R.drawable.ic_camera)
        .setIsSmartLockEnabled(false)
        .build()
      signInLauncher.launch(signInIntent)
    }
  }

  private fun followSelectedRole() {
    // while in development, ask each time
    showRoleSelectionDialog()
//    when (sharedPrefs.getString("role", null)) {
//      null -> showRoleSelectionDialog() // First launch
//      "camera" -> startCameraActivity()
//      "controller" -> startControllerActivity()
//    }
  }

  private fun onSignInResult(result: FirebaseAuthUIAuthenticationResult) {
    val response = result.idpResponse
    if (result.resultCode == RESULT_OK) {
      // successfully signed in
      followSelectedRole()
    } else {
      // sign in failed. If response is null the user canceled the
      // sign-in flow using the back button. Otherwise check
      // response.getError().getErrorCode() and handle the error.
    }
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