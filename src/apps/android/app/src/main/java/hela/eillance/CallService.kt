package hela.eillance

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Binder
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.Person
import hela.eillance.camera.R
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.ndkio.VideoConfig
import hela.eillance.ndkio.interfaces.IClientNativeBridge
import hela.eillance.webrtc.WebRtcTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import org.webrtc.VideoSink
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.atomic.AtomicBoolean

class CallService : Service(), IClientNativeBridge {

  inner class CallServiceBinder : Binder() {
    fun getService(): CallService = this@CallService
  }

  private val binder = CallServiceBinder()
  private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
  private val notificationManager by lazy { getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager }

  private var onStateChanged: ((IntArray) -> Unit)? = null
  private var wakeLock: PowerManager.WakeLock? = null
  private var webRtcTransport: WebRtcTransport? = null
  private val isEngineInitialized = AtomicBoolean(false)

  private var lastKnownCallState: CallState? = null
  private var lastKnownContacts: List<Contact>? = null

  // --- Public API for ViewModel ---
  fun startLocalMedia() = webRtcTransport?.startLocalMedia()
  fun stopLocalMedia() = webRtcTransport?.stopLocalMedia()
  fun setLocalVideoSink(sink: VideoSink?) = webRtcTransport?.setLocalSink(sink)
  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) = webRtcTransport?.setRemoteSink(peerId, sink)
  fun updateSignalingUrl(newUrl: String) = nativeUpdateSignalingUrl(newUrl)

  override fun onBind(intent: Intent?): IBinder = binder

  override fun onCreate() {
    super.onCreate()
    createNotificationChannels()
    serviceScope.launch {
      initEngine(this@CallService)
      isEngineInitialized.set(true)
      if (identityExists()) {
        start()
      }
      refreshState()
    }
    startForegroundService()
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      ACTION_REJECT_CALL -> rejectCall()
      ACTION_HANGUP -> hangup()
    }
    return START_STICKY
  }

  override fun onDestroy() {
    serviceScope.cancel()
    terminate()
    releaseWakeLock()
    super.onDestroy()
  }

  fun setStateChangedCallback(callback: ((IntArray) -> Unit)?) {
    onStateChanged = callback
  }

  @Suppress("unused")
  fun setWebRtcTransport(transport: WebRtcTransport) {
    webRtcTransport = transport
  }

  override fun onStateChanged(changeSet: IntArray) {
    onStateChanged?.invoke(changeSet)
  }

  fun handleNotifications() {
    val newCallState = nativeGetCallState()
    val newContacts = nativeGetContacts()

    checkForIncomingCall(lastKnownCallState, newCallState)
    checkForNewMessages(lastKnownContacts, newContacts)

    val connectionState = ServiceConnectionState.fromInt(nativeGetConnectionState())
    updateForegroundServiceNotification(newCallState?.isInCall ?: false, newCallState?.peerId, connectionState)

    lastKnownCallState = newCallState
    lastKnownContacts = newContacts
  }

  private fun checkForIncomingCall(oldState: CallState?, newState: CallState?) {
    val wasRinging = oldState?.isRinging ?: false
    val isRinging = newState?.isRinging ?: false

    if (!wasRinging && isRinging && !MercuryApplication.isAppInForeground) {
      showIncomingCallNotification(newState?.peerId ?: "Unknown Caller")
    } else if (wasRinging && !isRinging) {
      notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
    }
  }

  private fun checkForNewMessages(oldContacts: List<Contact>?, newContacts: List<Contact>?) {
    if (oldContacts == null || newContacts == null) return

    val oldUnreadCounts = oldContacts.associate { it.peerId to it.unreadCount }
    newContacts.forEach { newContact ->
      val oldUnread = oldUnreadCounts[newContact.peerId] ?: 0
      if (newContact.unreadCount > oldUnread && !MercuryApplication.isAppInForeground) {
        showNewMessageNotification(newContact.peerId, "New message from ${newContact.name}")
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

    val acceptIntent = Intent(this, MainActivity::class.java).apply {
      action = ACTION_ACCEPT_CALL
      putExtra(EXTRA_PEER_ID, peerId)
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
    }
    val acceptPendingIntent = PendingIntent.getActivity(this, 2, acceptIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

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
    val intent = Intent(this, MainActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
      putExtra(EXTRA_PEER_ID, peerId)
    }

    val pendingIntent = PendingIntent.getActivity(
      this,
      MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode(),
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
      .setSmallIcon(R.drawable.ic_status_standby)
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
      wakeLock?.acquire(10*60*1000L)
    }
  }

  private fun releaseWakeLock() {
    wakeLock?.let {
      if (it.isHeld) it.release()
    }
    wakeLock = null
  }

  fun acceptCall() {
    notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
    nativeAcceptCall()
  }

  fun rejectCall() {
    notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
    nativeRejectCall()
  }

  fun updateMyProfile(name: String?, bio: String?, photo: String?) = nativeUpdateMyProfile(name, bio, photo)
  fun setContactDisplayNameOverride(peerId: String, name: String) = nativeSetContactDisplayNameOverride(peerId, name)
  fun resetContactDisplayNameOverride(peerId: String) = nativeResetContactDisplayNameOverride(peerId)

  fun debugEraseAllHistory() = nativeDebugEraseAllHistory()
  fun debugEraseAllContacts() = nativeDebugEraseAllContacts()
  fun debugRevokeAllApprovals() = nativeDebugRevokeAllApprovals()

  fun createBackupToTemp(password: String): String? {
    return try {
        val tempFile = File.createTempFile("mercury-backup-", ".mcr", cacheDir)
        val success = nativeCreateBackup(tempFile.absolutePath, password)
        if (success) {
            tempFile.absolutePath
        } else {
            tempFile.delete() // clean up if backup failed
            null
        }
    } catch (e: Exception) {
        Log.e("CallService", "Error creating temporary backup file", e)
        null
    }
  }

  fun deleteTempBackup(path: String) {
    try {
        val file = File(path)
        if (file.exists()) {
            if (!file.delete()) {
                Log.w("CallService", "Failed to delete temporary backup file: $path")
            }
        }
    } catch (e: Exception) {
        Log.e("CallService", "Error deleting temporary backup file", e)
    }
  }

  fun restoreBackup(uri: Uri, password: String): Boolean {
    return try {
      val inputStream = contentResolver.openInputStream(uri) ?: return false
      val tempFile = File.createTempFile("restore_temp", ".mcr", cacheDir)
      FileOutputStream(tempFile).use { outputStream ->
        inputStream.use { it.copyTo(outputStream) }
      }
      val success = nativeRestoreBackup(tempFile.absolutePath, password)
      tempFile.delete()
      success
    } catch (e: Exception) {
      Log.e("CallService", "Error during backup restore", e)
      false
    }
  }

  fun generateNewIdentity(): String {
    val mnemonic = nativeGenerateNewIdentity()
    if (mnemonic.isNotEmpty()) {
      start()
    }
    return mnemonic
  }

  fun restoreIdentity(mnemonic: String): Boolean {
    val success = nativeRestoreIdentity(mnemonic)
    if (success) {
      start()
    }
    return success
  }

  fun getMnemonic(): String? = nativeGetMnemonic()

  private external fun initEngine(nativeBridge: IClientNativeBridge)
  external fun start()
  private external fun terminate()
  external fun identityExists(): Boolean

  fun refreshState() {
    if (isEngineInitialized.get()) {
      nativeRefreshState()
    } else {
      Log.d("CallService", "refreshState called before engine initialized; ignoring.")
    }
  }

  // --- NDK Query API ---
  external fun nativeGetMyProfile(): MyProfileInfo?
  external fun nativeGetContacts(): List<Contact>?
  external fun nativeGetInteractionHistoryPage(): HistoryPage?
  external fun nativeGetCallState(): CallState?
  external fun nativeGetConnectionState(): Int
  external fun nativeGetSignalingUrl(): String?
  external fun nativeGetSelectedContactId(): String?

  private external fun nativeRefreshState()
  external fun addContact(peerId: String, token: String?)
  external fun acceptContact(peerId: String)
  external fun blockContact(peerId: String)
  external fun deleteContact(peerId: String)
  external fun unblockContact(peerId: String)
  external fun selectContact(peerId: String)
  private external fun nativeGenerateContactToken(): String?
  external fun sendMessage(message: String)
  external fun startVoiceCall()
  external fun startVideoCall()
  external fun loadOlderMessages()
  external fun loadFullMessage(messageId: Long)
  external fun markMessagesAsRead(messageIds: LongArray)
  external fun configureVideoCapture(config: VideoConfig)
  external fun hangup()
  private external fun nativeUpdateSignalingUrl(url: String)
  private external fun nativeAcceptCall()
  private external fun nativeRejectCall()
  private external fun nativeUpdateMyProfile(name: String?, bio: String?, photo_base64: String?)
  private external fun nativeSetContactDisplayNameOverride(peerId: String, name: String)
  private external fun nativeResetContactDisplayNameOverride(peerId: String)
  private external fun nativeGetMnemonic(): String?
  private external fun nativeCreateBackup(path: String, password: String): Boolean
  private external fun nativeRestoreBackup(path: String, password: String): Boolean
  private external fun nativeGenerateNewIdentity(): String
  private external fun nativeRestoreIdentity(mnemonic: String): Boolean
  private external fun nativeDebugEraseAllHistory()
  private external fun nativeDebugEraseAllContacts()
  private external fun nativeDebugRevokeAllApprovals()

  fun generateContactToken(): String? = nativeGenerateContactToken()

  companion object {
    private const val FOREGROUND_SERVICE_NOTIFICATION_ID = 101
    const val INCOMING_CALL_NOTIFICATION_ID = 102
    const val MESSAGE_NOTIFICATION_ID_BASE = 1000
    private const val FOREGROUND_CHANNEL_ID = "foreground_service_channel"
    private const val CALLS_CHANNEL_ID = "incoming_calls_channel"
    private const val MESSAGES_CHANNEL_ID = "messages_channel"
    private const val ACTION_HANGUP = "hela.eillance.ACTION_HANGUP"
    const val ACTION_ACCEPT_CALL = "hela.eillance.ACTION_ACCEPT_CALL"
    private const val ACTION_REJECT_CALL = "hela.eillance.ACTION_REJECT_CALL"
    const val EXTRA_PEER_ID = "hela.eillance.EXTRA_PEER_ID"
  }
}
