package hela.eillance

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
import android.hardware.camera2.CameraMetadata
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Binder
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import androidx.core.graphics.drawable.DrawableCompat
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.preference.PreferenceManager
import hela.eillance.camera.R
import hela.eillance.events.EventSource
import hela.eillance.events.ServiceConnectionState
import hela.eillance.events.interfaces.INativeBridge
import hela.eillance.ndkio.VideoConfig
import java.util.concurrent.atomic.AtomicLong

class AppEngineService : Service(), INativeBridge {
  private var appEngine: AtomicLong = AtomicLong(0)
  private var serviceConnectionState: ServiceConnectionState = ServiceConnectionState.Unknown

  private lateinit var connectivityManager: ConnectivityManager
  private var networkCallback: ConnectivityManager.NetworkCallback? = null

  inner class StreamingServiceBinder : Binder() {
    val service: AppEngineService
      get() = this@AppEngineService
  }

  private val streamingServiceBinder = StreamingServiceBinder()

  override fun onCreate() {
    super.onCreate()
    if (isCamera2Device()) {
      appEngine.set(createAppEngine(this))
    } else {
      Log.e("Tune", "Found legacy camera device, camera2 device required")
      return
    }
    startForeground()
    setupNetworkMonitoring()
  }

  override fun onDestroy() {
    stopNetworkMonitoring()
    deleteAppEngine(appEngine.get())
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
        configureDeviceName(appEngine.get(), deviceName)

        val videoConfig = getVideoConfigPreferences()
        configureVideoCapture(appEngine.get(), videoConfig)
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
    val context = this@AppEngineService
    val drawable = ContextCompat.getDrawable(context, first)
    val color = ContextCompat.getColor(context, second)

    DrawableCompat.setTint(drawable!!, color)

    val bitmap = Bitmap.createBitmap(
      drawable.intrinsicWidth, drawable.intrinsicHeight, Bitmap.Config.ARGB_8888
    )
    val canvas = Canvas(bitmap)
    drawable.setBounds(0, 0, canvas.width, canvas.height)
    drawable.draw(canvas)
    return bitmap
  }

  private fun startForeground() {
    if (!isForegroundAllowed()) {
      Log.e("AppEngineService", "Cannot start foreground service in the current state.")
      return
    }

    val channel = createNotificationChannel()
    val statusIcon = when (serviceConnectionState) {
      ServiceConnectionState.Unknown -> Pair(R.drawable.ic_status1_standby, R.color.colorNegative)
      ServiceConnectionState.Establishing -> Pair(R.drawable.ic_status2, R.color.colorPositive)
      else -> Pair(R.drawable.ic_failure, R.color.colorNegative)
    }

    val mainActivityIntent = Intent(this, MainActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
    }

    val notification = NotificationCompat.Builder(this, channel)
      .setOngoing(true)
      .setLargeIcon(statusIcon.getBitmapFromVectorDrawable())
      .setSmallIcon(R.drawable.ic_camera_stream)
      .setContentTitle(getString(R.string.notification_title))
      .setContentText(serviceConnectionState.getResourceString(this))
      .setPriority(NotificationCompat.PRIORITY_LOW)
      .setContentIntent(
        PendingIntent.getActivity(
          this, 0, mainActivityIntent,
          PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
      ).build().apply {
        flags = Notification.FLAG_NO_CLEAR or Notification.FLAG_FOREGROUND_SERVICE
      }

    try {
      startForeground(CAMERA_SERVICE_FG_ID, notification)
    } catch (e: Exception) {
      Log.e("AppEngineService", "Failed to start foreground: ${e.message}")
    }
  }

  private fun isForegroundAllowed(): Boolean {
    // Ensure that the app is allowed to start a foreground service.
    val activityManager = getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
    return activityManager.appTasks.any { task ->
      task.taskInfo.baseIntent.component?.className?.contains("MainActivity") ?: false
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
          start()
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

  private external fun createAppEngine(nativeBridge: INativeBridge): Long

  private external fun deleteAppEngine(appMgr: Long)
  fun deleteAppEngine() {
    return deleteAppEngine(appEngine.getAndSet(0))
  }

  private external fun start(appMgr: Long)
  fun start() {
    return start(appEngine.get())
  }

  private external fun stop(appMgr: Long)
  fun stop() {
    return stop(appEngine.get())
  }

  private external fun getServiceConnectionState(appMgr: Long): ServiceConnectionState
  fun getServiceConnectionState(): ServiceConnectionState {
    return getServiceConnectionState(appEngine.get())
  }

  private external fun configureDeviceName(appMgr: Long, name: String)
  fun configureDeviceName(name: String) {
    return configureDeviceName(appEngine.get(), name)
  }

  private external fun configureVideoCapture(appMgr: Long, conf: VideoConfig)
  fun configureVideoCapture(conf: VideoConfig) {
    return configureVideoCapture(appEngine.get(), conf)
  }

  companion object {
    private const val CAMERA_SERVICE_FG_ID = 9990

    init {
      System.loadLibrary("app-engine-lib")
    }
  }
}
