package hela.eillance

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import hela.eillance.camera.R
import hela.eillance.ndkio.VideoConfig
import hela.eillance.ndkio.interfaces.IClientNativeBridge

class CallService : Service(), IClientNativeBridge {

  inner class CallServiceBinder : Binder() {
    fun getService(): CallService = this@CallService
  }

  private val binder = CallServiceBinder()
  private var onJsonStateUpdate: ((String) -> Unit)? = null

  override fun onBind(intent: Intent?): IBinder = binder

  override fun onCreate() {
    super.onCreate()
    initEngine(this)
    start()
    startForegroundService()
  }

  override fun onDestroy() {
    terminate()
    super.onDestroy()
  }

  fun setJsonStateCallback(callback: ((String) -> Unit)?) {
    onJsonStateUpdate = callback
  }

  // This is the new single callback from C++
  override fun onStateUpdate(jsonState: String) {
    onJsonStateUpdate?.invoke(jsonState)
  }

  private fun startForegroundService() {
    val channelId = createNotificationChannel()
    val notificationIntent = Intent(this, MainActivity::class.java)
    val pendingIntent = PendingIntent.getActivity(this, 0, notificationIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val notification: Notification = NotificationCompat.Builder(this, channelId)
      .setContentTitle(getString(R.string.app_name))
      .setContentText("Running in background")
      .setSmallIcon(R.drawable.ic_camera_stream)
      .setContentIntent(pendingIntent)
      .build()

    try {
      startForeground(SERVICE_ID, notification)
    } catch (e: Exception) {
      Log.e("CallService", "Failed to start foreground service", e)
    }
  }

  private fun createNotificationChannel(): String {
    val channelId = "call_service_channel"
    val channelName = "Call Service"
    val chan = NotificationChannel(channelId, channelName, NotificationManager.IMPORTANCE_NONE)
    chan.lockscreenVisibility = Notification.VISIBILITY_PRIVATE
    val service = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    service.createNotificationChannel(chan)
    return channelId
  }

  private external fun initEngine(nativeBridge: IClientNativeBridge)
  private external fun start()
  private external fun terminate()
  external fun refreshState()
  external fun initDecoder(surface: android.view.Surface)
  external fun releaseDecoder()
  external fun call(peerId: String)
  external fun addContact(peerId: String)
  external fun acceptContact(peerId: String)
  external fun selectContact(peerId: String)
  external fun sendMessage(message: String)
  external fun startVoiceCall()
  external fun startVideoCall()
  external fun loadOlderMessages()
  external fun markMessagesAsRead(messageIds: LongArray)
  external fun configureVideoCapture(config: VideoConfig)

  companion object {
    private const val SERVICE_ID = 101
    init {
      System.loadLibrary("lib-client-service-bridge")
    }
  }
}
