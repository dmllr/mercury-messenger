package hela.eillance

import android.content.Intent
import android.content.SharedPreferences
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.firebase.ui.auth.AuthUI
import com.firebase.ui.auth.FirebaseAuthUIActivityResultContract
import com.firebase.ui.auth.data.model.FirebaseAuthUIAuthenticationResult
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.firebase.auth.FirebaseAuth
import hela.eillance.camera.R
import hela.eillance.streamer.StreamerActivity
import hela.eillance.theme.EillanceTheme
import hela.eillance.utils.NetworkUtil
import hela.eillance.viewer.ViewerActivity

class LauncherActivity : ComponentActivity() {
    // authentication providers
    private val providers = arrayListOf(
        AuthUI.IdpConfig.EmailBuilder().build(),
        AuthUI.IdpConfig.PhoneBuilder().build(),
        AuthUI.IdpConfig.GoogleBuilder().build(),
    )
    private lateinit var sharedPrefs: SharedPreferences
    private lateinit var networkUtil: NetworkUtil

    // State for network and auth
    private var isCheckingNetwork by mutableStateOf(true)
    private var isNetworkAvailable by mutableStateOf(false)
    private var isNetworkDialogShowing by mutableStateOf(false)

    private val signInLauncher = registerForActivityResult(
        FirebaseAuthUIActivityResultContract(),
    ) { res ->
        this.onSignInResult(res)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        sharedPrefs = getSharedPreferences("app_prefs", MODE_PRIVATE)

        // Initialize network utility
        networkUtil = NetworkUtil(this)

        enableEdgeToEdge()

        setContent {
            EillanceTheme {
                val networkState by networkUtil.networkAvailable.observeAsState(initial = false)

                // Update state based on observed network
                LaunchedEffect(networkState) {
                    isNetworkAvailable = networkState
                    isCheckingNetwork = false

                    if (isNetworkAvailable && !isNetworkDialogShowing) {
                        // Check if user is authenticated when network becomes available
                        checkAuthAndProceed()
                    }
                }

                Surface(modifier = Modifier.fillMaxSize()) {
                    LauncherContent(
                        isCheckingNetwork = isCheckingNetwork,
                        isNetworkAvailable = isNetworkAvailable,
                        onRetry = { retryConnection() }
                    )
                }
            }
        }

        // Start network monitoring
        networkUtil.startMonitoring()
    }

    @Composable
    private fun LauncherContent(
        isCheckingNetwork: Boolean,
        isNetworkAvailable: Boolean,
        onRetry: () -> Unit
    ) {
        Box(modifier = Modifier.fillMaxSize()) {
            when {
                isCheckingNetwork -> {
                    // Show loading
                    Column(
                        modifier = Modifier
                            .align(Alignment.Center),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        CircularProgressIndicator()
                        Text(
                            text = "Checking connection...",
                            modifier = Modifier
                                .padding(top = 16.dp),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                }
                !isNetworkAvailable -> {
                    // Show network error
                    Column(
                        modifier = Modifier.align(Alignment.Center),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = "No internet connection",
                            color = MaterialTheme.colorScheme.error
                        )
                        androidx.compose.material3.Button(
                            onClick = onRetry,
                            modifier = Modifier
                                .padding(top = 48.dp)
                        ) {
                            Text("Retry")
                        }


                    }
                }
                // Network is available but UI is handled by normal flow
            }
        }
    }

    private fun retryConnection() {
        isCheckingNetwork = true

        // Small delay to show checking state
        val handler = android.os.Handler(android.os.Looper.getMainLooper())
        handler.postDelayed({
            isNetworkAvailable = networkUtil.isNetworkAvailable()
            isCheckingNetwork = false

            if (isNetworkAvailable) {
                checkAuthAndProceed()
            }
        }, 1000)
    }

    private fun checkAuthAndProceed() {
        if (FirebaseAuth.getInstance().currentUser == null) {
            launchSignInFlow()
        } else {
            followSelectedRole()
        }
    }

    private fun launchSignInFlow() {
        val signInIntent = AuthUI.getInstance()
            .createSignInIntentBuilder()
            .setAvailableProviders(providers)
            .setLogo(R.drawable.ic_camera)
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
            // sign in failed - check if network is still available
            if (networkUtil.isNetworkAvailable()) {
                // Show auth error dialog
                MaterialAlertDialogBuilder(this)
                    .setTitle("Authentication Failed")
                    .setMessage("Failed to sign in. Please try again.")
                    .setPositiveButton("Retry") { _, _ -> launchSignInFlow() }
                    .setNegativeButton("Cancel") { _, _ -> finish() }
                    .show()
            }
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

    override fun onDestroy() {
        super.onDestroy()
        networkUtil.stopMonitoring()
    }
}
