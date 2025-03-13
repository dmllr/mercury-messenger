package hela.eillance

import android.content.Intent
import android.content.SharedPreferences
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import com.firebase.ui.auth.AuthUI
import com.firebase.ui.auth.FirebaseAuthUIActivityResultContract
import com.firebase.ui.auth.data.model.FirebaseAuthUIAuthenticationResult
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.firebase.auth.FirebaseAuth
import hela.eillance.camera.R
import hela.eillance.streamer.StreamerActivity
import hela.eillance.theme.EillanceTheme
import hela.eillance.viewer.ViewerActivity

class LauncherActivity : ComponentActivity() {
    // authentication providers
    private val providers = arrayListOf(
        AuthUI.IdpConfig.EmailBuilder().build(),
        AuthUI.IdpConfig.PhoneBuilder().build(),
        AuthUI.IdpConfig.GoogleBuilder().build(),
    )
    private lateinit var sharedPrefs: SharedPreferences

    private val signInLauncher = registerForActivityResult(
        FirebaseAuthUIActivityResultContract(),
    ) { res ->
        this.onSignInResult(res)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        sharedPrefs = getSharedPreferences("app_prefs", MODE_PRIVATE)

        enableEdgeToEdge()

        setContent {
            EillanceTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    if (FirebaseAuth.getInstance().currentUser == null) {
                        launchSignInFlow()
                    } else {
                        followSelectedRole()
                    }
                }
            }
        }
    }

    private fun launchSignInFlow() {
        val signInIntent = AuthUI.getInstance()
            .createSignInIntentBuilder()
            .setAvailableProviders(providers)
            .setLogo(R.drawable.ic_camera)
            .setIsSmartLockEnabled(false)
            .build()
        signInLauncher.launch(signInIntent)
    }

    private fun followSelectedRole() {
        // while in development, ask each time
        showRoleSelectionDialog()
//    when (sharedPrefs.getString("role", null)) {
//      null -> showRoleSelectionDialog() // First launch
//      "camera" -> startCameraActivity()
//      "viewer" -> startViewerActivity()
    }


    private fun onSignInResult(result: FirebaseAuthUIAuthenticationResult?) {
        if (result?.resultCode == RESULT_OK) {
            // successfully signed in
            followSelectedRole()
        } else {
            // sign in failed.
        }
    }

    private fun showRoleSelectionDialog() {
        MaterialAlertDialogBuilder(this)
            .setTitle("Select Mode")
            .setMessage("Choose how you want to use this device")
            .setPositiveButton("Camera Device") { _, _ ->
                saveRoleAndLaunch("camera")
            }
            .setNegativeButton("Viewer Device") { _, _ ->
                saveRoleAndLaunch("viewer")
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
        "viewer" -> startViewerActivity()
    }
    finish()
}

private fun startCameraActivity() {
    startActivity(Intent(this, StreamerActivity::class.java))
    finish()
}

private fun startViewerActivity() {
    startActivity(Intent(this, ViewerActivity::class.java))
    finish()
}

}
