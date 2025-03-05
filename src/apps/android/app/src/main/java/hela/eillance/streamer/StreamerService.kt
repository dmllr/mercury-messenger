package hela.eillance.streamer

import android.app.ActivityManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Binder
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import androidx.core.graphics.createBitmap
import androidx.core.graphics.drawable.DrawableCompat
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.preference.PreferenceManager
import com.google.firebase.auth.FirebaseAuth
import hela.eillance.Tune
import hela.eillance.camera.R
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.ndkio.VideoConfig
import hela.eillance.ndkio.interfaces.IStreamerNativeBridge

class StreamerService : Service(), IStreamerNativeBridge {
  private var serviceConnectionState: ServiceConnectionState = ServiceConnectionState.Unknown

  private lateinit var connectivityManager: ConnectivityManager
  private var networkCallback: ConnectivityManager.NetworkCallback? = null

  inner class StreamingServiceBinder : Binder() {
    val service: StreamerService
      get() = this@StreamerService
  }

  private val streamingServiceBinder = StreamingServiceBinder()

  override fun onCreate() {
    super.onCreate()
    if (!isCamera2Device()) {
      Log.e(javaClass.simpleName, "Found legacy camera device, camera2 device required")
      return
    }
    initEngine(this)
    startForeground()
    setupNetworkMonitoring()
  }

  override fun onDestroy() {
    stopNetworkMonitoring()
    super.onDestroy()
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    intent?.let { startForeground() }
    return START_STICKY
  }

  override fun onBind(intent: Intent): IBinder {
    return streamingServiceBinder
  }

  override fun onStatusUpdate(peerId: String, source: Int, status: Int) {
    val intent = Intent(Tune::BroadcastActionOnStateUpdate.get())
    intent.putExtra(Tune::BroadcastActionOnStateUpdate_peer_id.get(), peerId)
    intent.putExtra(Tune::BroadcastActionOnStateUpdate_source.get(), source)
    intent.putExtra(Tune::BroadcastActionOnStateUpdate_state.get(), status)
    LocalBroadcastManager.getInstance(this).sendBroadcast(intent)

    if (source == EventSource.System.value) {
      serviceConnectionState = ServiceConnectionState.fromInt(status)

      if (status == ServiceConnectionState.Establishing.value) {
        val deviceName = getDeviceNamePreference()
        configureDeviceName(deviceName)

        val videoConfig = getVideoConfigPreferences()
        configureVideoCapture(videoConfig)
      }

      startForeground()
    }
  }

  private fun getDeviceNamePreference(): String =
    PreferenceManager.getDefaultSharedPreferences(this)
      .getString("device_name", "") ?: ""

  private fun getVideoConfigPreferences(): VideoConfig {
    val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(this)
    return VideoConfig().apply {
      cameraId = sharedPreferences.getString("default_camera", "0")?.toIntOrNull() ?: 0
      sharedPreferences.getString("camera_resolution", "0x0")
        ?.split("x")?.let {
          width = it.getOrNull(0)?.toIntOrNull() ?: 0
          height = it.getOrNull(1)?.toIntOrNull() ?: 0
        }
      frameRate = sharedPreferences.getString("frame_rate", "30")?.toIntOrNull() ?: 30
      bitrate = sharedPreferences.getString("bitrate", "0")?.toIntOrNull() ?: 0
    }
  }

  private fun isCamera2Device(): Boolean {
    val camMgr = getSystemService(Context.CAMERA_SERVICE) as CameraManager
    return try {
      camMgr.cameraIdList.any {
        val characteristics = camMgr.getCameraCharacteristics(it)
        characteristics.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL) !=
            CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY
      }
    } catch (e: CameraAccessException) {
      e.printStackTrace()
      false
    }
  }

  private fun Pair<Int, Int>.getBitmapFromVectorDrawable(): Bitmap {
    val context = this@StreamerService
    val drawable = ContextCompat.getDrawable(context, first)
    val color = ContextCompat.getColor(context, second)

    DrawableCompat.setTint(drawable!!, color)

    val bitmap = createBitmap(drawable.intrinsicWidth, drawable.intrinsicHeight)
    val canvas = Canvas(bitmap)
    drawable.setBounds(0, 0, canvas.width, canvas.height)
    drawable.draw(canvas)
    return bitmap
  }

  private fun startForeground() {
    if (!isForegroundAllowed()) {
      Log.e("StreamerService", "Cannot start foreground service in the current state.")
      return
    }

    val channel = createNotificationChannel()
    val statusIcon = when (serviceConnectionState) {
      ServiceConnectionState.Unknown -> Pair(R.drawable.ic_status1_standby, R.color.colorNegative)
      ServiceConnectionState.Establishing -> Pair(R.drawable.ic_status2, R.color.colorPositive)
      else -> Pair(R.drawable.ic_failure, R.color.colorNegative)
    }

    val streamerActivityIntent = Intent(this, StreamerActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
    }

    val notification = NotificationCompat.Builder(this, channel)
      .setOngoing(true)
      .setLargeIcon(statusIcon.getBitmapFromVectorDrawable())
      .setSmallIcon(R.drawable.ic_camera_stream)
      .setContentTitle(getString(R.string.notification_title))
      .setContentText(serviceConnectionState.getResourceString(this))
      .setPriority(NotificationCompat.PRIORITY_HIGH)
      .setContentIntent(
        PendingIntent.getActivity(
          this, 0, streamerActivityIntent,
          PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
      ).build().apply {
        flags = Notification.FLAG_NO_CLEAR or Notification.FLAG_FOREGROUND_SERVICE
      }

    try {
      startForeground(CAMERA_SERVICE_FG_ID, notification)
    } catch (e: Exception) {
      Log.e("StreamerService", "Failed to start foreground: ${e.message}")
    }
  }

  private fun isForegroundAllowed(): Boolean {
    // Ensure that the app is allowed to start a foreground service.
    val activityManager = getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
    return activityManager.appTasks.any { task ->
      task.taskInfo.baseIntent.component?.className?.contains("StreamerActivity") ?: false
    }
  }

  private fun createNotificationChannel(): String {
    val id = "surveillance-foreground-service"
    val service = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    service.createNotificationChannel(
      NotificationChannel(
        id, getString(R.string.notification_description),
        NotificationManager.IMPORTANCE_LOW
      ).apply { lockscreenVisibility = Notification.VISIBILITY_PRIVATE }
    )
    return id
  }

  private fun setupNetworkMonitoring() {
    connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    networkCallback = object : ConnectivityManager.NetworkCallback() {

      private val availableNetworks: MutableSet<Network> = HashSet()

      override fun onAvailable(network: Network) {
        val networkCapabilities = connectivityManager.getNetworkCapabilities(network)
        val hasInternetCapability =
          networkCapabilities?.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)

        if (hasInternetCapability == true) {
          availableNetworks.add(network)
          sendNetworkState()
        }
      }

      override fun onLost(network: Network) {
        availableNetworks.remove(network)
        sendNetworkState()
      }

      private fun sendNetworkState() {
        if (availableNetworks.isNotEmpty()) {
          checkAuthAndStart()
        } else {
          stop()
        }
      }
    }

    val request = NetworkRequest.Builder()
      .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
      .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
      .addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR)
      .build()
    connectivityManager.registerNetworkCallback(request, networkCallback!!)
  }

  private fun stopNetworkMonitoring() {
    networkCallback?.let {
      connectivityManager.unregisterNetworkCallback(it)
    }
  }

  fun checkAuthAndStart() {
    val user = FirebaseAuth.getInstance().currentUser
    user?.getIdToken(true)?.addOnCompleteListener { task ->
      if (task.isSuccessful) {
        val idToken = task.result?.token
        if (idToken != null) {
          updateAuthToken(idToken)
          start()
        }
      } else {
        Log.e("FirebaseAuth", "Failed to get ID token", task.exception)
      }
    }
  }

  private external fun initEngine(nativeBridge: IStreamerNativeBridge)
  private external fun updateAuthToken(token: String)
  external fun start()
  external fun stop()
  external fun getServiceConnectionState(): ServiceConnectionState
  external fun configureDeviceName(name: String)
  external fun configureVideoCapture(conf: VideoConfig)

  companion object {
    private const val CAMERA_SERVICE_FG_ID = 9990

    init {
      System.loadLibrary("lib-streamer-service-bridge")
    }
  }
}
