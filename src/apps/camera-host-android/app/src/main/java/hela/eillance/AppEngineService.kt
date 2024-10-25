package hela.eillance

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


class AppEngineService : Service(), INativeBridge {
  private var appEngine: Long = 0
  private var serviceConnectionState: ServiceConnectionState = ServiceConnectionState.Unknown

  inner class StreamingServiceBinder : Binder() {
    val service: AppEngineService
      get() = this@AppEngineService
  }

  private val streamingServiceBinder = StreamingServiceBinder()

  override fun onCreate() {
    super.onCreate()

    if (isCamera2Device()) {
      createAppEngine()
    } else {
      Log.e(Tune.TAG, "Found legacy camera device, camera2 device required")
      // TODO treat error
      return
    }

    startForeground()
  }

  override fun onDestroy() {
    deleteAppEngine(appEngine)

    super.onDestroy()
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    if (intent != null)
      startForeground()

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

  private fun getDeviceNamePreference(): String {
    val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(this)
    val deviceName = sharedPreferences.getString("device_name", "")
    return deviceName!!
  }

  private fun getVideoConfigPreferences(): VideoConfig {
    val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(this)

    val videoConfig = VideoConfig()
    videoConfig.cameraId = sharedPreferences.getString("default_camera", "0")?.toIntOrNull() ?: 0
    val resolution = sharedPreferences.getString("camera_resolution", "0x0")?.split("x")
    videoConfig.width = resolution?.get(0)?.toIntOrNull() ?: 0
    videoConfig.height = resolution?.get(1)?.toIntOrNull() ?: 0
    videoConfig.frameRate = sharedPreferences.getString("frame_rate", "30")?.toIntOrNull() ?: 30
    videoConfig.bitrate = sharedPreferences.getString("bitrate", "0")?.toIntOrNull() ?: 0

    return videoConfig
  }

  private fun isCamera2Device(): Boolean {
    val camMgr = getSystemService(Context.CAMERA_SERVICE) as CameraManager
    var camera2Dev = true
    try {
      val cameraIds = camMgr.cameraIdList
      if (cameraIds.isNotEmpty()) {
        for (id in cameraIds) {
          val characteristics = camMgr.getCameraCharacteristics(id)
          val deviceLevel =
            characteristics.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)!!
          val facing = characteristics.get(CameraCharacteristics.LENS_FACING)!!
          if (deviceLevel == CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY && facing == CameraMetadata.LENS_FACING_BACK)
            camera2Dev = false
        }
      }
    } catch (e: CameraAccessException) {
      e.printStackTrace()
      camera2Dev = false
    }

    return camera2Dev
  }

  private fun createAppEngine() {
    appEngine = createAppEngine(this)
  }

  fun getServiceConnectionState(): ServiceConnectionState {
    return getServiceConnectionState(appEngine)
  }

  fun configureDeviceName(name: String) {
    configureDeviceName(appEngine, name)
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
    val channel = createNotificationChannel()

    val customText = serviceConnectionState.getResourceString(this)
    val statusIcon = when (serviceConnectionState) {
      ServiceConnectionState.Unknown -> Pair(R.drawable.ic_status1_standby, R.color.colorNegative)
      ServiceConnectionState.Establishing -> Pair(R.drawable.ic_status2, R.color.colorPositive)
      ServiceConnectionState.Online -> Pair(R.drawable.ic_status1_standby, R.color.colorPositive)
      ServiceConnectionState.Failure -> Pair(R.drawable.ic_failure, R.color.colorNegative)
    }

    val mainActivityIntent = Intent(this, MainActivity::class.java)
    mainActivityIntent.flags = Intent.FLAG_ACTIVITY_REORDER_TO_FRONT

    val pendingIntent = PendingIntent.getActivity(
      this,
      0,
      mainActivityIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val notificationBuilder = NotificationCompat.Builder(this, channel)
    val builder = notificationBuilder.setOngoing(true)
      .setLargeIcon(statusIcon.getBitmapFromVectorDrawable())
      .setSmallIcon(R.drawable.ic_camera_stream)
      .setContentTitle(getString(R.string.notification_title))
      .setContentText(customText)
      .setPriority(NotificationCompat.PRIORITY_LOW)
      .setCategory(NotificationCompat.CATEGORY_SERVICE)
      .setContentIntent(pendingIntent)

    val notification = builder.build()
    notification.flags = Notification.FLAG_NO_CLEAR or Notification.FLAG_FOREGROUND_SERVICE

    startForeground(CAMERA_SERVICE_FG_ID, notification)
  }

  private fun createNotificationChannel(): String {
    val id = "surveillance-foreground-service"
    val chan = NotificationChannel(
      id,
      getString(R.string.notification_description),
      NotificationManager.IMPORTANCE_LOW
    )
    chan.lockscreenVisibility = Notification.VISIBILITY_PRIVATE

    val service = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

    service.createNotificationChannel(chan)

    return id
  }

  // native lib functions
  private external fun createAppEngine(nativeBridge: INativeBridge): Long
  private external fun deleteAppEngine(appMgr: Long)
  private external fun getServiceConnectionState(appMgr: Long): ServiceConnectionState
  private external fun configureDeviceName(appMgr: Long, name: String)
  external fun configureVideoCapture(conf: VideoConfig)

  companion object {
    private const val CAMERA_SERVICE_FG_ID = 9990

    init {
      System.loadLibrary("app-engine-lib")
    }
  }
}
