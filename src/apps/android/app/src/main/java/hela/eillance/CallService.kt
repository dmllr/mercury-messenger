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
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import hela.eillance.camera.R
import hela.eillance.ndkio.AppState
import hela.eillance.ndkio.VideoConfig
import hela.eillance.ndkio.interfaces.IClientNativeBridge
import hela.eillance.webrtc.WebRtcTransport
import kotlinx.serialization.json.Json

class CallService : Service(), IClientNativeBridge {

  inner class CallServiceBinder : Binder() {
    fun getService(): CallService = this@CallService
  }

  private val binder = CallServiceBinder()
  private var onJsonStateUpdate: ((String) -> Unit)? = null
  private val json = Json { ignoreUnknownKeys = true }
  private var wakeLock: PowerManager.WakeLock? = null
  private var webRtcTransport: WebRtcTransport? = null

  // --- Service Lifecycle ---
  override fun onBind(intent: Intent?): IBinder = binder

  override fun onCreate() {
    super.onCreate()
    webRtcTransport = WebRtcTransport(this, 0L) // Pass a dummy pointer for now
    initEngine(this)
    start()
    startForegroundService()
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    if (intent?.action == ACTION_HANGUP) {
      hangup()
    }
    return START_STICKY
  }

  override fun onDestroy() {
    terminate()
    webRtcTransport?.terminate()
    releaseWakeLock()
    super.onDestroy()
  }

  fun setJsonStateCallback(callback: ((String) -> Unit)?) {
    onJsonStateUpdate = callback
  }

  // This is the new single callback from C++
  override fun onStateUpdate(jsonState: String) {
    onJsonStateUpdate?.invoke(jsonState)
    try {
      val newState = json.decodeFromString<AppState>(jsonState)
      updateNotification(newState.callState.isInCall, newState.callState.peerId)
    } catch (e: Exception) {
      Log.w("CallService", "Failed to parse state for notification update", e)
    }
  }

  private fun startForegroundService() {
    val channelId = createNotificationChannel()
    val notification = buildNotification(false, null, channelId)
    try {
      startForeground(SERVICE_ID, notification)
    } catch (e: Exception) {
      Log.e("CallService", "Failed to start foreground service", e)
    }
  }

  private fun updateNotification(inCall: Boolean, peerId: String?) {
    if (inCall) acquireWakeLock() else releaseWakeLock()

    val channelId = createNotificationChannel()
    val notification = buildNotification(inCall, peerId, channelId)
    val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    manager.notify(SERVICE_ID, notification)
  }

  private fun buildNotification(inCall: Boolean, peerId: String?, channelId: String): Notification {
    val notificationIntent = Intent(this, MainActivity::class.java)
    val pendingIntent = PendingIntent.getActivity(this, 0, notificationIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val builder = NotificationCompat.Builder(this, channelId)
      .setContentTitle(getString(R.string.app_name))
      .setSmallIcon(R.drawable.ic_camera_stream)
      .setContentIntent(pendingIntent)

    if (inCall) {
      val hangupIntent = Intent(this, CallService::class.java).apply { action = ACTION_HANGUP }
      val hangupPendingIntent = PendingIntent.getService(this, 0, hangupIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
      builder.setContentText("In call with ${peerId ?: "Unknown"}")
        .addAction(0, "Hang Up", hangupPendingIntent)
    } else {
      builder.setContentText("Ready for connections")
    }

    return builder.build()
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

  private fun acquireWakeLock() {
    if (wakeLock == null) {
      val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
      wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Mercury::CallWakeLock")
      wakeLock?.setReferenceCounted(false)
      wakeLock?.acquire(10*60*1000L /*10 minutes timeout*/)
    }
  }

  private fun releaseWakeLock() {
    wakeLock?.let {
      if (it.isHeld) {
        it.release()
      }
    }
    wakeLock = null
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
  external fun hangup()
  external fun acceptCall()
  external fun rejectCall()
  external fun loadOlderMessages()
  external fun markMessagesAsRead(messageIds: LongArray)
  external fun configureVideoCapture(config: VideoConfig)

  companion object {
    private const val SERVICE_ID = 101
    private const val ACTION_HANGUP = "hela.eillance.ACTION_HANGUP"
    init {
      System.loadLibrary("lib-client-service-bridge")
    }
  }
}
