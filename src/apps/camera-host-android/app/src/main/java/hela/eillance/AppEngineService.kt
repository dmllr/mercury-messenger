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
import hela.eillance.camera.R
import hela.eillance.events.EventSource
import hela.eillance.events.interfaces.IStatusUpdateCallback
import hela.eillance.events.StateService
import hela.eillance.events.getResourceString
import hela.eillance.ndkio.Status


class AppEngineService : Service(), IStatusUpdateCallback {
  private var appEngine: Long = 0
  private var underlyingServicesStates: MutableMap<EventSource, StateService> = mutableMapOf(
    EventSource.ConnectionStatus to StateService.Unknown,
  )

  inner class StreamingServiceBinder : Binder() {
    val service: AppEngineService
      get() = this@AppEngineService
  }

  // AppEngine callback interface

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

  override fun onStatusUpdate(source: Int, status: Int) {
    val intent = Intent(Tune::BroadcastActionOnStateUpdate.get())
    intent.putExtra(Tune::BroadcastActionOnStateUpdate_source.get(), source)
    intent.putExtra(Tune::BroadcastActionOnStateUpdate_state.get(), status)
    sendBroadcast(intent)

    val src = EventSource.fromInt(source)
    if (src == EventSource.ConnectionStatus) {
      val state = StateService.fromInt(status)

      underlyingServicesStates[src] = state
      Log.d(Tune.TAG, "Status update: $src/$state")

      startForeground()
    }
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

  fun getStatus(): Status {
    return getStatus(appEngine)
  }

  private fun Pair<Int, Int>.getBitmapFromVectorDrawable(): Bitmap {
    val context = this@AppEngineService
    val drawable = ContextCompat.getDrawable(context, first)
    val color = ContextCompat.getColor(context, second)

    DrawableCompat.setTint(drawable!!, color)

    val bitmap = Bitmap.createBitmap(
      drawable.intrinsicWidth, drawable.intrinsicHeight, Bitmap.Config.ARGB_8888)
    val canvas = Canvas(bitmap)
    drawable.setBounds(0, 0, canvas.width, canvas.height)
    drawable.draw(canvas)
    return bitmap
  }

  private fun startForeground() {
    val channel = createNotificationChannel()

    val customText =
      underlyingServicesStates[EventSource.ConnectionStatus]?.getResourceString(this)
    val statusIcon = when (underlyingServicesStates[EventSource.ConnectionStatus]) {
      StateService.Standby -> Pair(R.drawable.ic_status1_standby, R.color.colorPositive)
      StateService.Setup   -> Pair(R.drawable.ic_status2,         R.color.colorPositive)
      StateService.Action  -> Pair(R.drawable.ic_in_action,       R.color.colorPositive)
      StateService.Failure -> Pair(R.drawable.ic_failure,         R.color.colorNegative)
      else                 -> Pair(R.drawable.ic_status1_standby, R.color.colorNegative)
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
  private external fun createAppEngine(statusCallback: IStatusUpdateCallback): Long
  private external fun deleteAppEngine(ndkCamera: Long)
  private external fun getStatus(ndkCamera: Long): Status

  companion object {
    private const val CAMERA_SERVICE_FG_ID = 9990

    init {
      System.loadLibrary("camera-lib")
    }
  }
}
