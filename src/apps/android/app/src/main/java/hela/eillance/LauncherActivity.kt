package hela.eillance

import android.content.Intent
import android.content.SharedPreferences
import android.content.res.Configuration
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CheckboxDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
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
    // Authentication providers
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

    // Role selection state
    private var isShowingRoleSelection by mutableStateOf(false)

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
                    if (isShowingRoleSelection) {
                        // Show role selection UI
                        RoleSelectionScreen(
                            initialAskEachTime = sharedPrefs.getBoolean("ask_each_time", false),
                            onCameraSelected = { askEachTime ->
                                saveRoleAndLaunch("camera", askEachTime)
                            },
                            onViewerSelected = { askEachTime ->
                                saveRoleAndLaunch("viewer", askEachTime)
                            }
                        )
                    } else {
                        // Show network check UI
                        LauncherContent(
                            isCheckingNetwork = isCheckingNetwork,
                            isNetworkAvailable = isNetworkAvailable,
                            onRetry = { retryConnection() }
                        )
                    }
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
                            text = stringResource(R.string.launcher_checking_connection),
                            modifier = Modifier.padding(top = 16.dp),
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
                            text = stringResource(R.string.launcher_no_internet_connection),
                            color = MaterialTheme.colorScheme.error
                        )
                        Button(
                            onClick = onRetry,
                            modifier = Modifier.padding(top = 48.dp)
                        ) {
                            Text(stringResource(R.string.retry))
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
        val handler = Handler(Looper.getMainLooper())
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
            .setLogo(R.drawable.ic_camera_stream)
            .setTheme(R.style.FirebaseAuthTheme)
            .setTosAndPrivacyPolicyUrls(
                "https://edu.eillance.com/legal/terms-and-conditions/",
                "https://edu.eillance.com/legal/privacy-policy/"
            )
            .setAlwaysShowSignInMethodScreen(true) //for the app name on the login screen
            .build()
        signInLauncher.launch(signInIntent)
    }

    private fun followSelectedRole() {
        val savedAskEachTime = sharedPrefs.getBoolean("ask_each_time", false)
        val savedRole = sharedPrefs.getString("role", null)

        if (!savedAskEachTime && savedRole != null) {
            // If we have a saved role and we're not asking each time, launch directly
            when (savedRole) {
                "camera" -> startCameraActivity()
                "viewer" -> startViewerActivity()
            }
        } else {
            // Show role selection UI
            isShowingRoleSelection = true
        }
    }

    private fun onSignInResult(result: FirebaseAuthUIAuthenticationResult?) {
        if (result?.resultCode == RESULT_OK) {
            // Successfully signed in
            followSelectedRole()
        } else {
            // Sign in failed - check if network is still available
            if (networkUtil.isNetworkAvailable()) {
                // Show auth error dialog
                isNetworkDialogShowing = true
                MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.launcher_authentication_failed)
                    .setMessage(R.string.launcher_failed_to_sign_in)
                    .setPositiveButton(R.string.retry) { _, _ ->
                        isNetworkDialogShowing = false
                        launchSignInFlow()
                    }
                    .setNegativeButton(android.R.string.cancel) { _, _ ->
                        isNetworkDialogShowing = false
                        finish()
                    }
                    .setOnDismissListener { isNetworkDialogShowing = false }
                    .show()
            }
        }
    }

    private fun saveRoleAndLaunch(role: String, askEachTime: Boolean) {
        with(sharedPrefs.edit()) {
            if (askEachTime) {
                // If "Ask each time" is checked, don't save a persistent role preference
                remove("role")
            } else {
                putString("role", role)
            }
            // Save the "ask each time" preference
            putBoolean("ask_each_time", askEachTime)
            apply()
        }

        when (role) {
            "camera" -> startCameraActivity()
            "viewer" -> startViewerActivity()
        }
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

@Composable
fun RoleSelectionScreen(
    initialAskEachTime: Boolean = false,
    onCameraSelected: (askEachTime: Boolean) -> Unit,
    onViewerSelected: (askEachTime: Boolean) -> Unit
) {
    var selectedRole by remember { mutableStateOf<String?>(null) }
    var askEachTime by remember { mutableStateOf(initialAskEachTime) }

    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        MaterialTheme.colorScheme.background,
                        MaterialTheme.colorScheme.surfaceVariant,
                    )
                )
            )
            .padding(24.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize(),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            if (isLandscape) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(16.dp, Alignment.Start)
                ) {
                    // Logo
                    Box(
                        modifier = Modifier
                            .size(48.dp)
                            .clip(CircleShape)
                            .background(MaterialTheme.colorScheme.primaryContainer),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            painter = painterResource(id = R.drawable.ic_camera_stream),
                            contentDescription = "App Logo",
                            modifier = Modifier.size(32.dp),
                            tint = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                    }

                    // App Name and Title Column
                    Column(
                        horizontalAlignment = Alignment.Start
                    ) {
                        Text(
                            text = stringResource(R.string.app_name),
                            fontSize = 18.sp,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.primary
                        )
                        Text(
                            text = stringResource(R.string.launcher_select_device_role),
                            fontSize = 12.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Spacer(modifier = Modifier.height(16.dp))

            } else {
                // --- Portrait Header (Vertical Column) ---
                Spacer(modifier = Modifier.height(48.dp))

                // Logo and title
                Box(
                    modifier = Modifier
                        .size(80.dp)
                        .clip(CircleShape)
                        .background(MaterialTheme.colorScheme.primaryContainer),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        painter = painterResource(id = R.drawable.ic_camera_stream),
                        contentDescription = "App Logo",
                        modifier = Modifier.size(48.dp),
                        tint = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                }

                Spacer(modifier = Modifier.height(24.dp))

                Text(
                    text = stringResource(R.string.app_name),
                    fontSize = 28.sp,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary
                )

                Spacer(modifier = Modifier.height(8.dp))

                Text(
                    text = stringResource(R.string.launcher_select_device_role),
                    fontSize = 16.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )

                Spacer(modifier = Modifier.height(48.dp))
            }


            // Role options
            RoleOptionCards(
                selectedRole = selectedRole,
                onRoleSelected = { role ->
                    selectedRole = role
                }
            )

            Spacer(modifier = Modifier.weight(1f))

            // Ask each time checkbox
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth()
            ) {
                Checkbox(
                    checked = askEachTime,
                    onCheckedChange = { askEachTime = it },
                    colors = CheckboxDefaults.colors(
                        checkedColor = MaterialTheme.colorScheme.primary
                    )
                )

                Text(
                    text = stringResource(R.string.launcher_ask_each_time),
                    fontSize = 16.sp,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(start = 8.dp)
                )
            }

            Spacer(modifier = Modifier.height(16.dp))

            // Continue button
            AnimatedVisibility(
                visible = selectedRole != null,
                enter = fadeIn(animationSpec = tween(300)),
                exit = fadeOut(animationSpec = tween(300))
            ) {
                Button(
                    onClick = {
                        when (selectedRole) {
                            "camera" -> onCameraSelected(askEachTime)
                            "viewer" -> onViewerSelected(askEachTime)
                        }
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp),
                    shape = RoundedCornerShape(16.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary
                    )
                ) {
                    Text(
                        text = stringResource(R.string.continue_),
                        fontSize = 16.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }

            Spacer(modifier = Modifier.height(24.dp))
        }
    }
}

@Composable
fun RoleOptionCards(
    selectedRole: String?,
    onRoleSelected: (String) -> Unit
) {
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height((if (isLandscape) 120 else 280).dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Camera role card
        RoleCard(
            title = stringResource(R.string.launcher_role_camera),
            description = stringResource(R.string.launcher_role_streamer_description),
            iconResId = R.drawable.ic_camera,
            isSelected = selectedRole == "camera",
            primaryColor = MaterialTheme.colorScheme.primary,
            onClick = { onRoleSelected("camera") },
            modifier = Modifier.weight(1f)
        )

        // Viewer role card
        RoleCard(
            title = stringResource(R.string.launcher_role_viewer),
            description = stringResource(R.string.launcher_role_viewer_description),
            iconResId = R.drawable.ic_viewer,
            isSelected = selectedRole == "viewer",
            primaryColor = MaterialTheme.colorScheme.tertiary,
            onClick = { onRoleSelected("viewer") },
            modifier = Modifier.weight(1f)
        )
    }
}

@Composable
fun RoleCard(
    title: String,
    description: String,
    iconResId: Int,
    isSelected: Boolean,
    primaryColor: Color,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val elevation by animateDpAsState(
        targetValue = if (isSelected) 8.dp else 2.dp,
        animationSpec = tween(300),
        label = "elevation"
    )

    val borderWidth by animateDpAsState(
        targetValue = if (isSelected) 3.dp else 0.dp,
        animationSpec = tween(300),
        label = "borderWidth"
    )

    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE

    Card(
        modifier = modifier
            .fillMaxHeight(),
        shape = RoundedCornerShape(24.dp),
        border = BorderStroke(
            width = borderWidth,
            color = primaryColor
        ),
        elevation = CardDefaults.cardElevation(
            defaultElevation = elevation
        ),
        onClick = onClick
    ) {
        if (isLandscape) {
            // --- Landscape Layout (Row) ---
            Row(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp), // Space between icon/title and description
                verticalAlignment = Alignment.Top
            ) {
                // Left Column (Icon + Title)
                Column(
                    modifier = Modifier
                        .weight(0.4f)
                        .fillMaxHeight(),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    // Icon
                    Box(
                        modifier = Modifier
                            .size(56.dp)
                            .clip(CircleShape)
                            .background(
                                if (isSelected) primaryColor
                                else primaryColor.copy(alpha = 0.33f)
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            painter = painterResource(id = iconResId),
                            contentDescription = title,
                            modifier = Modifier.size(30.dp),
                            tint = if (isSelected)
                                MaterialTheme.colorScheme.onPrimary
                            else MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    // Title
                    Text(
                        text = title,
                        fontSize = 18.sp,
                        fontWeight = FontWeight.Bold,
                        color = if (isSelected) primaryColor
                        else MaterialTheme.colorScheme.onSurface,
                        textAlign = TextAlign.Center
                    )
                }

                // Right Column (Description)
                Column(
                    modifier = Modifier
                        .weight(0.6f)
                        .fillMaxHeight(),
                    horizontalAlignment = Alignment.Start
                ) {
                    Text(
                        text = description,
                        fontSize = 14.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Start
                    )
                }
            }

        } else {
            // --- Portrait Layout (Original Column) ---
            Column(
                modifier = Modifier.fillMaxSize(),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                // Icon Box
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(0.3f),
                    contentAlignment = Alignment.Center
                ) {
                    Box(
                        modifier = Modifier
                            .size(64.dp)
                            .clip(CircleShape)
                            .background(
                                if (isSelected) primaryColor
                                else primaryColor.copy(alpha = 0.33f)
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            painter = painterResource(id = iconResId),
                            contentDescription = title,
                            modifier = Modifier.size(32.dp),
                            tint = if (isSelected)
                                MaterialTheme.colorScheme.onPrimary
                            else MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }

                // Title Box
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(0.2f),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = title,
                        fontSize = 20.sp,
                        fontWeight = FontWeight.Bold,
                        color = if (isSelected) primaryColor
                        else MaterialTheme.colorScheme.onSurface
                    )
                }

                // Description Box
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(0.5f)
                        .padding(horizontal = 16.dp),
                    contentAlignment = Alignment.TopStart
                ) {
                    Text(
                        text = description,
                        fontSize = 14.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Start
                    )
                }
            }
        }
    }
}
