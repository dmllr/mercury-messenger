package hela.eillance

import android.content.Intent
import android.content.SharedPreferences
import android.content.res.Configuration
import android.os.Bundle
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
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
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
import hela.eillance.camera.R
import hela.eillance.streamer.StreamerActivity
import hela.eillance.theme.EillanceTheme
import hela.eillance.viewer.ViewerActivity

class LauncherActivity : ComponentActivity() {

    private lateinit var sharedPrefs: SharedPreferences

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        sharedPrefs = getSharedPreferences("app_prefs", MODE_PRIVATE)

        enableEdgeToEdge()

        setContent {
            EillanceTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    RoleSelectionScreen(
                        initialAskEachTime = sharedPrefs.getBoolean("ask_each_time", false),
                        onCameraSelected = { askEachTime ->
                            saveRoleAndLaunch("camera", askEachTime)
                        },
                        onViewerSelected = { askEachTime ->
                            saveRoleAndLaunch("viewer", askEachTime)
                        }
                    )
                }
            }
        }
        
        followSelectedRole()
    }

    private fun followSelectedRole() {
        val savedAskEachTime = sharedPrefs.getBoolean("ask_each_time", false)
        val savedRole = sharedPrefs.getString("role", null)

        if (!savedAskEachTime && savedRole != null) {
            when (savedRole) {
                "camera" -> startCameraActivity()
                "viewer" -> startViewerActivity()
            }
        }
    }

    private fun saveRoleAndLaunch(role: String, askEachTime: Boolean) {
        with(sharedPrefs.edit()) {
            if (askEachTime) {
                remove("role")
            } else {
                putString("role", role)
            }
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
                Spacer(modifier = Modifier.height(48.dp))
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

            RoleOptionCards(
                selectedRole = selectedRole,
                onRoleSelected = { role ->
                    selectedRole = role
                }
            )

            Spacer(modifier = Modifier.weight(1f))

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
        RoleCard(
            title = stringResource(R.string.launcher_role_camera),
            description = stringResource(R.string.launcher_role_streamer_description),
            iconResId = R.drawable.ic_camera,
            isSelected = selectedRole == "camera",
            primaryColor = MaterialTheme.colorScheme.primary,
            onClick = { onRoleSelected("camera") },
            modifier = Modifier.weight(1f)
        )
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
            Row(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.Top
            ) {
                Column(
                    modifier = Modifier
                        .weight(0.4f)
                        .fillMaxHeight(),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
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
                    Text(
                        text = title,
                        fontSize = 18.sp,
                        fontWeight = FontWeight.Bold,
                        color = if (isSelected) primaryColor
                        else MaterialTheme.colorScheme.onSurface,
                        textAlign = TextAlign.Center
                    )
                }
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
            Column(
                modifier = Modifier.fillMaxSize(),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
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
