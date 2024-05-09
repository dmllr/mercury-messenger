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
import android.util.Size
import android.view.Surface
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import androidx.core.graphics.drawable.DrawableCompat
import hela.eillance.camera.R
import hela.eillance.events.EventSource
import hela.eillance.events.IStatusCallback
import hela.eillance.events.ServiceState


class CameraService : Service(), IStatusCallback {
  private var log: String = ""
  private var cameraEngine: Long = 0
  var cameraPreviewSize: Size = Size(0, 0)
  private var underlyingServicesStates: MutableMap<EventSource, ServiceState> = mutableMapOf(
    EventSource.ConnectionStatus to ServiceState.Unknown,
  )

  inner class StreamingServiceBinder : Binder() {
    val service: CameraService
      get() = this@CameraService
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
    deleteAppEngine(cameraEngine)

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
    val src = EventSource.fromInt(source)
    if (src == EventSource.ConnectionStatus) {
      val state = ServiceState.fromInt(status)

      underlyingServicesStates[src] = state
      Log.d(Tune.TAG, "Status update: $src/$state")
      log += "$src/$state\n"

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
    cameraEngine = createAppEngine(this)
  }

  private fun Pair<Int, Int>.getBitmapFromVectorDrawable(): Bitmap {
    val context = this@CameraService
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
        "RTC service: ${underlyingServicesStates[EventSource.ConnectionStatus]}\n$log"
    val statusIcon = when (underlyingServicesStates[EventSource.ConnectionStatus]) {
      ServiceState.Standby -> Pair(R.drawable.ic_status1_standby, R.color.colorPositive)
      ServiceState.Setup   -> Pair(R.drawable.ic_status2,         R.color.colorPositive)
      ServiceState.Action  -> Pair(R.drawable.ic_in_action,       R.color.colorPositive)
      ServiceState.Failure -> Pair(R.drawable.ic_failure,         R.color.colorNegative)
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
      .setStyle(NotificationCompat.BigTextStyle(notificationBuilder))
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

  fun startPreview(preview: Boolean) {
    requestPreview(cameraEngine, preview)
  }

  fun previewSurfaceCreated(surface: Surface) {
    onPreviewSurfaceCreated(cameraEngine, surface)
  }

  fun previewSurfaceDestroyed(surface: Surface) {
    onPreviewSurfaceDestroyed(cameraEngine, surface)
  }


  // native lib functions
  private external fun createAppEngine(statusCallback: IStatusCallback): Long
  private external fun deleteAppEngine(ndkCamera: Long)

  private external fun requestPreview(ndkCamera: Long, preview: Boolean)

  private external fun onPreviewSurfaceCreated(ndkCamera: Long, surface: Surface)
  private external fun onPreviewSurfaceDestroyed(ndkCamera: Long, surface: Surface)

  companion object {
    private const val CAMERA_SERVICE_FG_ID = 9990

    init {
      System.loadLibrary("camera-lib")
    }
  }
}
