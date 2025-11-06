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
import androidx.core.app.Person
import hela.eillance.camera.R
import hela.eillance.ndkio.AppState
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.ndkio.VideoConfig
import hela.eillance.ndkio.interfaces.IClientNativeBridge
import hela.eillance.webrtc.WebRtcTransport
import kotlinx.serialization.json.Json
import org.webrtc.VideoSink

class CallService : Service(), IClientNativeBridge {

  inner class CallServiceBinder : Binder() {
    fun getService(): CallService = this@CallService
  }

  private val binder = CallServiceBinder()
  private val json = Json { ignoreUnknownKeys = true }
  private val notificationManager by lazy { getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager }

  private var onJsonStateUpdate: ((String) -> Unit)? = null
  private var wakeLock: PowerManager.WakeLock? = null
  private var webRtcTransport: WebRtcTransport? = null // This will be set by the native code

  // --- Public API for ViewModel ---
  fun startLocalMedia() = webRtcTransport?.startLocalMedia()
  fun stopLocalMedia() = webRtcTransport?.stopLocalMedia()
  fun setLocalVideoSink(sink: VideoSink?) = webRtcTransport?.setLocalSink(sink)
  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) = webRtcTransport?.setRemoteSink(peerId, sink)
  fun updateSignalingUrl(newUrl: String) {
    // This call updates the URL in the core and triggers a reconnect.
    nativeUpdateSignalingUrl(newUrl)
  }

  // --- Service Lifecycle ---
  override fun onBind(intent: Intent?): IBinder = binder
  private var previousState: AppState? = null

  override fun onCreate() {
    super.onCreate()
    createNotificationChannels()
    // The native engine initialization involves disk I/O and network setup,
    // which can be slow and must not block the main thread.
    Thread {
      initEngine(this)
      start()
    }.start()
    startForegroundService()
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      ACTION_ACCEPT_CALL -> {
        acceptCall()
      }
      ACTION_REJECT_CALL -> {
        rejectCall()
      }
      ACTION_HANGUP -> hangup()
    }
    return START_STICKY
  }

  override fun onDestroy() {
    terminate()
    releaseWakeLock()
    super.onDestroy()
  }

  fun setJsonStateCallback(callback: ((String) -> Unit)?) {
    onJsonStateUpdate = callback
  }

  // Called from C++ during initialization
  fun setWebRtcTransport(transport: WebRtcTransport) {
    webRtcTransport = transport
  }

  // This is the new single callback from C++
  override fun onStateUpdate(jsonState: String) {
    // Immediately forward the raw JSON to the UI to ensure it's always up-to-date
    try {
      val newState = json.decodeFromString<AppState>(jsonState)
      onJsonStateUpdate?.invoke(jsonState)

      // Handle notifications based on state changes
      checkForIncomingCall(previousState, newState)
      checkForNewMessages(previousState, newState)

      val connectionState = ServiceConnectionState.fromInt(newState.connectionState)
      updateForegroundServiceNotification(newState.callState.isInCall, newState.callState.peerId, connectionState)

      previousState = newState
    } catch (e: Exception) {
      Log.e("CallService", "Failed to parse AppState from C++", e)
    }
  }

  private fun checkForIncomingCall(oldState: AppState?, newState: AppState) {
    val wasRinging = oldState?.callState?.isRinging ?: false
    val isRinging = newState.callState.isRinging

    if (!wasRinging && isRinging && !MercuryApplication.isAppInForeground) {
      showIncomingCallNotification(newState.callState.peerId)
    } else if (wasRinging && !isRinging) {
      notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
    }
  }

  private fun checkForNewMessages(oldState: AppState?, newState: AppState) {
    if (oldState == null) return

    val oldChats = oldState.chats.associateBy { it.peerId }
    newState.chats.forEach { newChat ->
      val oldChat = oldChats[newChat.peerId]
      val lastMessage = newChat.lastMessage

      if (lastMessage.direction == hela.eillance.ndkio.MessageDirection.Received) {
        val oldLastMessageId = oldChat?.lastMessage?.id ?: -1
        if (lastMessage.id > oldLastMessageId && !MercuryApplication.isAppInForeground) {
          showNewMessageNotification(newChat.peerId, lastMessage.content ?: "New message")
        }
      }
    }
  }

  private fun startForegroundService() {
    val notification = buildForegroundServiceNotification(false, null, ServiceConnectionState.Unknown)
    try {
      startForeground(FOREGROUND_SERVICE_NOTIFICATION_ID, notification)
    } catch (e: Exception) {
      Log.e("CallService", "Failed to start foreground service", e)
    }
  }

  private fun updateForegroundServiceNotification(inCall: Boolean, peerId: String?, connectionState: ServiceConnectionState) {
    if (inCall) acquireWakeLock() else releaseWakeLock()
    val notification = buildForegroundServiceNotification(inCall, peerId, connectionState)
    notificationManager.notify(FOREGROUND_SERVICE_NOTIFICATION_ID, notification)
  }

  private fun showIncomingCallNotification(peerId: String) {
    val fullScreenIntent = Intent(this, MainActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
    }
    val fullScreenPendingIntent = PendingIntent.getActivity(this, 0, fullScreenIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val rejectIntent = Intent(this, CallService::class.java).apply { action = ACTION_REJECT_CALL }
    val rejectPendingIntent = PendingIntent.getService(this, 1, rejectIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val acceptIntent = Intent(this, CallService::class.java).apply { action = ACTION_ACCEPT_CALL }
    val acceptPendingIntent = PendingIntent.getService(this, 2, acceptIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val builder = NotificationCompat.Builder(this, CALLS_CHANNEL_ID)
      .setSmallIcon(android.R.drawable.stat_notify_chat)
      .setContentTitle("Incoming Call")
      .setContentText(peerId)
      .setPriority(NotificationCompat.PRIORITY_MAX)
      .setCategory(NotificationCompat.CATEGORY_CALL)
      .setFullScreenIntent(fullScreenPendingIntent, true)
      .addAction(0, "Reject", rejectPendingIntent)
      .addAction(0, "Accept", acceptPendingIntent)
      .setAutoCancel(true)
      .setOngoing(true)

    notificationManager.notify(INCOMING_CALL_NOTIFICATION_ID, builder.build())
  }

  private fun showNewMessageNotification(peerId: String, content: String) {
    // Create an Intent that will start the MainActivity.
    val intent = Intent(this, MainActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
      // Add extra data to identify which chat to open.
      putExtra(EXTRA_PEER_ID, peerId)
    }

    // This PendingIntent will launch the activity and deliver the intent.
    val pendingIntent = PendingIntent.getActivity(
      this,
      MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode(), // Unique request code per peer
      intent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val person = Person.Builder().setName(peerId).build()
    val messagingStyle = NotificationCompat.MessagingStyle(person)
      .addMessage(content, System.currentTimeMillis(), person)

    val builder = NotificationCompat.Builder(this, MESSAGES_CHANNEL_ID)
      .setSmallIcon(android.R.drawable.stat_notify_chat)
      .setStyle(messagingStyle)
      .setContentIntent(pendingIntent)
      .setAutoCancel(true)

    notificationManager.notify(MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode(), builder.build())
  }

  private fun buildForegroundServiceNotification(inCall: Boolean, peerId: String?, connectionState: ServiceConnectionState): Notification {
    val notificationIntent = Intent(this, MainActivity::class.java)
    val pendingIntent = PendingIntent.getActivity(this, 0, notificationIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val builder = NotificationCompat.Builder(this, FOREGROUND_CHANNEL_ID)
      .setContentTitle(getString(R.string.app_name))
      .setSmallIcon(android.R.drawable.stat_notify_chat)
      .setContentIntent(pendingIntent)

    if (inCall) {
      val hangupIntent = Intent(this, CallService::class.java).apply { action = ACTION_HANGUP }
      val hangupPendingIntent = PendingIntent.getService(this, 3, hangupIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
      builder.setContentText("In call with ${peerId ?: "Unknown"}")
        .addAction(0, "Hang Up", hangupPendingIntent)
    } else {
      val contentText = when (connectionState) {
        ServiceConnectionState.Online -> "Ready for connections"
        ServiceConnectionState.Establishing -> "Connecting..."
        ServiceConnectionState.Failure -> "Connection failed"
        ServiceConnectionState.Disconnected -> "Offline"
        ServiceConnectionState.InvalidToken -> "Authentication error"
        else -> "Initializing..."
      }
      builder.setContentText(contentText)
    }
    return builder.build()
  }

  private fun createNotificationChannels() {
    val foregroundChannel = NotificationChannel(FOREGROUND_CHANNEL_ID, "Foreground Service", NotificationManager.IMPORTANCE_MIN)
    val callsChannel = NotificationChannel(CALLS_CHANNEL_ID, "Incoming Calls", NotificationManager.IMPORTANCE_HIGH).apply {
      description = "Notifications for incoming voice and video calls"
    }
    val messagesChannel = NotificationChannel(MESSAGES_CHANNEL_ID, "New Messages", NotificationManager.IMPORTANCE_DEFAULT).apply {
      description = "Notifications for new chat messages"
    }
    notificationManager.createNotificationChannel(foregroundChannel)
    notificationManager.createNotificationChannel(callsChannel)
    notificationManager.createNotificationChannel(messagesChannel)
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

  // --- Public wrappers for native methods ---

  fun acceptCall() {
    notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
    nativeAcceptCall()
  }

  fun rejectCall() {
    notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
    nativeRejectCall()
  }

  // --- Public wrappers for profile methods ---
  fun updateMyProfile(name: String?, bio: String?, photo: String?) = nativeUpdateMyProfile(name, bio, photo)
  fun setContactDisplayNameOverride(peerId: String, name: String) = nativeSetContactDisplayNameOverride(peerId, name)
  fun resetContactDisplayNameOverride(peerId: String) = nativeResetContactDisplayNameOverride(peerId)

  // --- Debug Methods ---
  fun debugEraseAllHistory() = nativeDebugEraseAllHistory()
  fun debugEraseAllContacts() = nativeDebugEraseAllContacts()
  fun debugRevokeAllApprovals() = nativeDebugRevokeAllApprovals()

  private external fun initEngine(nativeBridge: IClientNativeBridge)
  private external fun start()
  private external fun terminate()
  external fun refreshState()
  external fun addContact(peerId: String)
  external fun acceptContact(peerId: String)
  external fun selectContact(peerId: String)
  external fun sendMessage(message: String)
  external fun startVoiceCall()
  external fun startVideoCall()
  external fun loadOlderMessages()
  external fun loadFullMessage(messageId: Long)
  external fun markMessagesAsRead(messageIds: LongArray)
  external fun configureVideoCapture(config: VideoConfig)
  external fun hangup()
  // Native implementations for wrapped methods
  private external fun nativeUpdateSignalingUrl(url: String)
  private external fun nativeAcceptCall()
  private external fun nativeRejectCall()
  private external fun nativeUpdateMyProfile(name: String?, bio: String?, photo_base64: String?)
  private external fun nativeSetContactDisplayNameOverride(peerId: String, name: String)
  private external fun nativeResetContactDisplayNameOverride(peerId: String)

  // --- Native Debug Methods ---
  private external fun nativeDebugEraseAllHistory()
  private external fun nativeDebugEraseAllContacts()
  private external fun nativeDebugRevokeAllApprovals()


  companion object {
    private const val FOREGROUND_SERVICE_NOTIFICATION_ID = 101
    const val INCOMING_CALL_NOTIFICATION_ID = 102
    const val MESSAGE_NOTIFICATION_ID_BASE = 1000

    private const val FOREGROUND_CHANNEL_ID = "foreground_service_channel"
    private const val CALLS_CHANNEL_ID = "incoming_calls_channel"
    private const val MESSAGES_CHANNEL_ID = "messages_channel"

    private const val ACTION_HANGUP = "hela.eillance.ACTION_HANGUP"
    private const val ACTION_ACCEPT_CALL = "hela.eillance.ACTION_ACCEPT_CALL"
    private const val ACTION_REJECT_CALL = "hela.eillance.ACTION_REJECT_CALL"

    const val EXTRA_PEER_ID = "hela.eillance.EXTRA_PEER_ID"
  }
}
