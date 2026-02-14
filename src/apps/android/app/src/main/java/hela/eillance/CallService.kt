package hela.eillance

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.os.PowerManager
import android.telecom.Connection
import android.telecom.PhoneAccount
import android.telecom.PhoneAccountHandle
import android.telecom.TelecomManager
import android.telecom.VideoProfile
import android.util.Log
import androidx.annotation.Keep
import androidx.core.app.NotificationCompat
import androidx.core.app.Person
import androidx.core.content.ContextCompat
import hela.eillance.camera.R
import hela.eillance.ndkio.AttachmentType
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.telecom.MercuryConnection
import hela.eillance.telecom.MercuryConnectionService
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.SettingsViewModel
import hela.eillance.webrtc.WebRtcDataTransport
import hela.eillance.webrtc.WebRtcMediaTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import org.webrtc.VideoSink

class CallService : Service() {

  class CallServiceBinder : android.os.Binder()

  private val binder = CallServiceBinder()
  private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
  private val notificationManager by lazy { getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager }
  private val audioManager by lazy { getSystemService(Context.AUDIO_SERVICE) as AudioManager }
  private val telecomManager by lazy { getSystemService(Context.TELECOM_SERVICE) as TelecomManager }
  private val engineMutex = Mutex()

  private var wakeLock: PowerManager.WakeLock? = null
  private var dataTransport: WebRtcDataTransport? = null
  private var mediaTransport: WebRtcMediaTransport? = null

  // Background ViewModels
  private var backgroundContactsVM: ContactsViewModel? = null
  var backgroundCallVM: CallViewModel? = null
    private set
  private var backgroundSettingsVM: SettingsViewModel? = null

  private var lastKnownCallState: CallState? = null
  private var lastKnownContacts: List<Contact>? = null

  private val soundPlayer: CallSoundPlayer by lazy { CallSoundPlayer(this) }
  private lateinit var phoneAccountHandle: PhoneAccountHandle

  // Track active telecom connection
  private var activeConnection: MercuryConnection? = null

  private val _errorEvents = MutableSharedFlow<String>()
  val errorEvents: SharedFlow<String> = _errorEvents.asSharedFlow()

  private val _isCoreUnresponsive = MutableStateFlow(false)
  val isCoreUnresponsive = _isCoreUnresponsive.asStateFlow()

  override fun onBind(intent: Intent?): IBinder = binder

  override fun onCreate() {
    super.onCreate()
    instance = this
    createNotificationChannels()
    registerPhoneAccount()

    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        try {
          initEngine(this@CallService)
          if (identityExists()) {
            start()
          }
          setupBackgroundObservers()
          _isEngineReady.value = true
          startWatchdog()
        } catch (e: Exception) {
          Log.e(TAG, "Failed to initialize engine", e)
        }
      }
    }

    startForegroundService()
  }

  private fun startWatchdog() {
    serviceScope.launch(Dispatchers.IO) {
      while (true) {
        delay(10000)
        if (_isEngineReady.value) {
          val isAlive = try {
            withTimeout(2000) { nativePing() }
          } catch (e: Exception) {
            false
          }
          _isCoreUnresponsive.value = !isAlive
        }
      }
    }
  }

  fun restartCore() {
    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        stopLocalMedia()
        nativeRestart()
        _isCoreUnresponsive.value = false
        _errorEvents.emit(getString(R.string.core_reset_success))
      }
    }
  }

  @Keep
  fun onNativeCoreCrash() {
    restartCore()
  }

  private fun setupBackgroundObservers() {
    val app = application
    backgroundContactsVM = ContactsViewModel(app)
    backgroundCallVM = CallViewModel(app)
    backgroundSettingsVM = SettingsViewModel(app)

    serviceScope.launch {
      launch {
        backgroundContactsVM?.contacts?.collect { contacts ->
          checkForNewMessages(lastKnownContacts, contacts)
          lastKnownContacts = contacts
        }
      }
      launch {
        backgroundCallVM?.callState?.collect { state ->
          handleCallStateChange(state)
        }
      }
      launch {
        backgroundSettingsVM?.connectionState?.collect { state ->
          updateForegroundServiceNotification(lastKnownCallState?.isInCall == true, lastKnownCallState?.peerId, state)
        }
      }
    }
  }

  // --- Telecom & Call Logic ---

  fun acceptCall() {
    // Stop ringing immediately
    soundPlayer.stop()
    cancelIncomingCallNotification()

    // Notify Telecom if valid
    activeConnection?.let {
      if (it.state == Connection.STATE_RINGING) {
        it.setActive()
      }
    }

    // Trigger Native Logic via bridge to avoid loop
    backgroundCallVM?.nativeAcceptCallBridge()

    // Ensure local media starts and is ENABLED
    val isVideo = lastKnownCallState?.callType == HistoryEntryKind.VideoCall
    startLocalMedia(isVideo)
  }

  fun rejectCall() {
    Log.i(TAG, "rejectCall requested")
    soundPlayer.stop()
    cancelIncomingCallNotification()

    activeConnection?.let {
      if (it.state != Connection.STATE_DISCONNECTED) {
        it.setDisconnected(android.telecom.DisconnectCause(android.telecom.DisconnectCause.REJECTED))
        it.destroy()
      }
    }
    activeConnection = null

    backgroundCallVM?.nativeRejectCallBridge()
  }

  fun hangup() {
    Log.i(TAG, "hangup requested")
    soundPlayer.stop()

    activeConnection?.let {
      if (it.state != Connection.STATE_DISCONNECTED) {
        it.setDisconnected(android.telecom.DisconnectCause(android.telecom.DisconnectCause.LOCAL))
        it.destroy()
      }
    }
    activeConnection = null

    backgroundCallVM?.nativeHangupBridge()
  }

  // Called from MercuryConnection when Telecom initiates the disconnect
  fun onTelecomDisconnect(connection: MercuryConnection) {
    if (activeConnection == connection) {
      activeConnection = null
      soundPlayer.stop()
      // If we are still in call natively, hangup
      val currentState = lastKnownCallState
      if (currentState != null && (currentState.isRinging || currentState.isInCall || currentState.isConnecting)) {
        if (currentState.isRinging) {
          backgroundCallVM?.nativeRejectCallBridge()
        } else {
          backgroundCallVM?.nativeHangupBridge()
        }
      }
    }
  }

  fun startLocalMedia(isVideo: Boolean) {
    mediaTransport?.startLocalMedia(isVideo)
  }

  fun stopLocalMedia() = mediaTransport?.stopLocalMedia()
  fun setLocalVideoSink(sink: VideoSink?) = mediaTransport?.setLocalSink(sink)
  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) = mediaTransport?.setRemoteSink(peerId, sink)

  fun setMicrophoneMute(muted: Boolean) {
    mediaTransport?.setAudioEnabled(!muted)
  }

  fun setSpeakerphoneOn(on: Boolean) {
    audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      val devices = audioManager.availableCommunicationDevices
      val targetType = if (on) AudioDeviceInfo.TYPE_BUILTIN_SPEAKER else AudioDeviceInfo.TYPE_BUILTIN_EARPIECE
      val device = devices.find { it.type == targetType }
      if (device != null) {
        audioManager.setCommunicationDevice(device)
      } else {
        audioManager.clearCommunicationDevice()
      }
    } else {
      @Suppress("DEPRECATION")
      audioManager.isSpeakerphoneOn = on
    }
  }

  private fun registerPhoneAccount() {
    val componentName = ComponentName(this, MercuryConnectionService::class.java)
    phoneAccountHandle = PhoneAccountHandle(componentName, getString(R.string.telecom_account_name))
    val builder = PhoneAccount.builder(phoneAccountHandle, getString(R.string.telecom_account_name))
      .setCapabilities(PhoneAccount.CAPABILITY_SELF_MANAGED)
      .setShortDescription(getString(R.string.telecom_account_description))
    try {
      telecomManager.registerPhoneAccount(builder.build())
    } catch (e: SecurityException) {
      Log.e(TAG, "Failed to register PhoneAccount", e)
    }
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      ACTION_REJECT_CALL -> rejectCall()
      ACTION_HANGUP -> hangup()
      ACTION_MARK_READ -> {
        val peerId = intent.getStringExtra(EXTRA_PEER_ID)
        if (peerId != null) {
          serviceScope.launch {
            CallService.isEngineReady.first { it }
            backgroundContactsVM?.markAllAsRead(peerId)
            notificationManager.cancel(MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode())
          }
        }
      }
    }
    return START_STICKY
  }

  override fun onDestroy() {
    instance = null
    _isEngineReady.value = false
    soundPlayer.stop()
    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        terminate()
      }
    }
    releaseWakeLock()
    super.onDestroy()
  }

  @Suppress("unused")
  fun setWebRtcDataTransport(transport: WebRtcDataTransport) {
    dataTransport = transport
    dataTransport?.onError = { errorMessage ->
      serviceScope.launch { _errorEvents.emit(errorMessage) }
    }
  }

  @Suppress("unused")
  fun setWebRtcMediaTransport(transport: WebRtcMediaTransport) {
    mediaTransport = transport
    mediaTransport?.onSendSignalingMessage = { msg ->
      try {
        val json = JSONObject(msg)
        val peerId = json.optString("to")
        if (!peerId.isNullOrEmpty()) {
          dataTransport?.sendDataChannelMessage(peerId, msg)
        }
      } catch (e: Exception) {
        Log.e(TAG, "Failed to route media signaling message", e)
      }
    }
  }

  // --- State Handling ---

  private fun handleCallStateChange(callState: CallState) {
    // 1. Update Sound
    soundPlayer.updateCallState(callState)

    // 2. Incoming Call?
    val wasRinging = lastKnownCallState?.isRinging == true
    if (!wasRinging && callState.isRinging && callState.callDirection == CallDirection.Incoming) {
      processIncomingCall(callState)
    }

    // 3. Update Telecom State
    syncTelecomState(callState)

    // 4. Update Notifications
    if (!callState.isRinging) {
      // If we stopped ringing (Accepted or Rejected), kill the incoming notification
      cancelIncomingCallNotification()
    }
    updateForegroundServiceNotification(callState.isInCall, callState.peerId, ServiceConnectionState.Online)

    // 5. Strict Media Security Enforcement
    // Ensure absolutely NO media flows unless the call is active (accepted).
    val wasInCall = lastKnownCallState?.isInCall == true
    val isInCall = callState.isInCall

    if (!wasInCall && isInCall) {
      // Transition to Active: Unmute media to allow communication
      val isVideo = callState.callType == HistoryEntryKind.VideoCall
      mediaTransport?.setAudioEnabled(true)
      if (isVideo) mediaTransport?.setVideoEnabled(true)
    } else if (!isInCall) {
      // Not active (Connecting, Ringing, Ended): FORCE DISABLE MEDIA
      // This prevents "hot mic" during ringing or connection setup.
      mediaTransport?.setAudioEnabled(false)
      mediaTransport?.setVideoEnabled(false)
    }

    val wasActive = lastKnownCallState?.let { it.isConnecting || it.isRinging || it.isInCall } ?: false
    val isActive = callState.isConnecting || callState.isRinging || callState.isInCall

    if (wasActive && !isActive) {
      stopLocalMedia()
      audioManager.mode = AudioManager.MODE_NORMAL
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        audioManager.clearCommunicationDevice()
      } else {
        @Suppress("DEPRECATION")
        audioManager.isSpeakerphoneOn = false
      }
    }

    lastKnownCallState = callState
  }

  private fun processIncomingCall(callState: CallState) {
    try {
      val extras = Bundle()
      extras.putString(MercuryConnectionService.EXTRA_PEER_ID, callState.peerId)
      val uri = Uri.fromParts(PhoneAccount.SCHEME_SIP, callState.peerId, null)
      extras.putParcelable(TelecomManager.EXTRA_INCOMING_CALL_ADDRESS, uri)

      // Android Telecom wraps extras sometimes
      val nestedExtras = Bundle()
      nestedExtras.putString(MercuryConnectionService.EXTRA_PEER_ID, callState.peerId)
      extras.putBundle(TelecomManager.EXTRA_INCOMING_CALL_EXTRAS, nestedExtras)

      telecomManager.addNewIncomingCall(phoneAccountHandle, extras)
    } catch (e: Exception) {
      Log.e(TAG, "Failed to add incoming call to Telecom", e)
      // Fallback if Telecom fails
      showIncomingCallNotification(callState.peerId)
    }
  }

  private fun syncTelecomState(callState: CallState) {
    // If we are in a call but Telecom is not active/ringing, we might need to sync up.
    // However, usually Telecom drives us or we drive Telecom via accept/reject methods.
    // This method ensures we kill Telecom if C++ hangs up remotely.

    val isActiveCall = callState.isConnecting || callState.isRinging || callState.isInCall

    if (!isActiveCall && activeConnection != null) {
      // C++ says idle, but Telecom is active -> Remote Hangup
      Log.i(TAG, "Sync: C++ idle, disconnecting Telecom")
      activeConnection?.setDisconnected(android.telecom.DisconnectCause(android.telecom.DisconnectCause.REMOTE))
      activeConnection?.destroy()
      activeConnection = null
    } else if (callState.isInCall && activeConnection?.state == Connection.STATE_RINGING) {
      // C++ says Connected, Telecom says Ringing -> We accepted via UI/Auto
      Log.i(TAG, "Sync: C++ connected, setting Telecom active")
      activeConnection?.setActive()
    }
  }

  // Called by MainActivity.onStop to ensure notification is shown if the user leaves the app
  // while a call is incoming.
  fun onActivityStopped() {
    val currentState = lastKnownCallState ?: backgroundCallVM?.callState?.value
    if (currentState != null && currentState.isRinging && currentState.callDirection == CallDirection.Incoming) {
      // Post notification immediately since UI is no longer visible
      showIncomingCallNotification(currentState.peerId)
    }
  }

  // Called by MercuryConnectionService when a new connection is created
  fun onConnectionCreated(connection: MercuryConnection, peerId: String) {
    activeConnection = connection
  }

  private fun cancelIncomingCallNotification() {
    notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
  }

  private fun checkForNewMessages(oldContacts: List<Contact>?, newContacts: List<Contact>) {
    if (oldContacts == null) return
    val oldUnreadCounts = oldContacts.associate { it.peerId to it.unreadCount }
    newContacts.forEach { newContact ->
      val oldUnread = oldUnreadCounts[newContact.peerId] ?: 0
      if (newContact.unreadCount > oldUnread && !MercuryApplication.isAppInForeground) {
        val prefs = getSharedPreferences("mercury_prefs", Context.MODE_PRIVATE)
        val showContent = prefs.getBoolean("show_notifications_content", true)
        val displayName = newContact.name.ifEmpty { newContact.peerId.take(8) }
        val unreadMessages = backgroundContactsVM?.getUnreadMessages(newContact.peerId) ?: emptyList()
        showNewMessageNotification(newContact.peerId, displayName, unreadMessages, showContent)
      }
    }
  }

  private fun startForegroundService() {
    val notification = buildForegroundServiceNotification(false, null, ServiceConnectionState.Unknown)
    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        startForeground(
          FOREGROUND_SERVICE_NOTIFICATION_ID,
          notification,
          ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        )
      } else {
        startForeground(FOREGROUND_SERVICE_NOTIFICATION_ID, notification)
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to start foreground service", e)
    }
  }

  private fun updateForegroundServiceNotification(
    inCall: Boolean,
    peerId: String?,
    connectionState: ServiceConnectionState
  ) {
    if (inCall) acquireWakeLock() else releaseWakeLock()
    val notification = buildForegroundServiceNotification(inCall, peerId, connectionState)
    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        val serviceType = if (inCall) {
          var type = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
          if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (ContextCompat.checkSelfPermission(
                this,
                Manifest.permission.RECORD_AUDIO
              ) == PackageManager.PERMISSION_GRANTED
            ) {
              type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
            }
            val isVideo = lastKnownCallState?.callType == HistoryEntryKind.VideoCall
            val hasCamera =
              ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED
            if (isVideo && hasCamera) {
              type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA
            }
          }
          type
        } else {
          ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        }
        startForeground(FOREGROUND_SERVICE_NOTIFICATION_ID, notification, serviceType)
      } else {
        notificationManager.notify(FOREGROUND_SERVICE_NOTIFICATION_ID, notification)
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to update foreground service notification", e)
    }
  }

  fun showIncomingCallNotification(peerId: String) {
    // If the app is already in the foreground, the IncomingCallScreen is visible.
    // We suppress the notification to avoid clutter.
    // If the user navigates away (onStop), this method will be called again with isAppInForeground = false.
    if (MercuryApplication.isAppInForeground) {
      return
    }

    val fullScreenIntent = Intent(this, MainActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
      action = ACTION_SHOW_INCOMING_CALL
      putExtra(EXTRA_PEER_ID, peerId)
    }
    val fullScreenPendingIntent = PendingIntent.getActivity(
      this,
      12345,
      fullScreenIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
    val rejectIntent = Intent(this, CallService::class.java).apply { action = ACTION_REJECT_CALL }
    val rejectPendingIntent =
      PendingIntent.getService(this, 1, rejectIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
    val acceptIntent = Intent(this, MainActivity::class.java).apply {
      action = ACTION_ACCEPT_CALL
      putExtra(EXTRA_PEER_ID, peerId)
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
    }
    val acceptPendingIntent = PendingIntent.getActivity(
      this,
      2,
      acceptIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val builder = NotificationCompat.Builder(this, CALLS_CHANNEL_ID)
      .setSmallIcon(android.R.drawable.stat_notify_chat)
      .setContentTitle(getString(R.string.notification_incoming_call_title))
      .setContentText(peerId)
      .setPriority(NotificationCompat.PRIORITY_MAX)
      .setCategory(NotificationCompat.CATEGORY_CALL)
      .setFullScreenIntent(fullScreenPendingIntent, true)
      .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
      .addAction(0, getString(R.string.action_reject), rejectPendingIntent)
      .addAction(0, getString(R.string.action_accept), acceptPendingIntent)
      .setAutoCancel(false) // Important: Don't auto cancel on tap
      .setOngoing(true)

    // CRITICAL: Explicitly start the activity to force it to the front if the device is unlocked.
    // We can do this because we are a bound ConnectionService (Background Activity Start Exemption).
    // We only do this if we aren't already in the foreground to avoid lifecycle thrashing.
    if (!MercuryApplication.isAppInForeground) {
      try {
        startActivity(fullScreenIntent)
      } catch (e: Exception) {
        Log.e(TAG, "Failed to force start incoming call activity", e)
      }
    }

    notificationManager.notify(INCOMING_CALL_NOTIFICATION_ID, builder.build())
  }

  private fun showNewMessageNotification(
    peerId: String,
    title: String,
    messages: List<HistoryEntry>,
    showContent: Boolean
  ) {
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
    val me = Person.Builder().setName(getString(R.string.app_name)).build()
    val sender = Person.Builder().setName(title).build()
    val style = NotificationCompat.MessagingStyle(me)

    if (messages.isNotEmpty()) {
      messages.forEach { msg ->
        val text = if (showContent) {
          if (!msg.content.isNullOrBlank()) msg.content
          else if (msg.attachments.isNotEmpty()) {
            val att = msg.attachments.first()
            val typeText = when (att.type) {
              AttachmentType.Image -> getString(R.string.file_type_image)
              AttachmentType.Video -> getString(R.string.file_type_video)
              AttachmentType.File -> if (att.mime.startsWith("audio/")) getString(R.string.file_type_audio) else getString(
                R.string.file_type_generic
              )
            }
            "[$typeText]"
          } else getString(R.string.notification_new_message)
        } else getString(R.string.notification_new_message)
        style.addMessage(text, msg.timestamp / 1000, sender)
      }
    } else {
      style.addMessage(getString(R.string.notification_new_message), System.currentTimeMillis(), sender)
    }

    val markReadIntent = Intent(this, CallService::class.java).apply {
      action = ACTION_MARK_READ
      putExtra(EXTRA_PEER_ID, peerId)
    }
    val markReadPendingIntent = PendingIntent.getService(
      this,
      peerId.hashCode(),
      markReadIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
    val markReadAction =
      NotificationCompat.Action.Builder(0, getString(R.string.action_mark_read), markReadPendingIntent).build()

    val builder = NotificationCompat.Builder(this, MESSAGES_CHANNEL_ID)
      .setSmallIcon(android.R.drawable.stat_notify_chat)
      .setStyle(style)
      .setShowWhen(false)
      .setContentIntent(pendingIntent)
      .addAction(markReadAction)
      .setAutoCancel(true)
    notificationManager.notify(MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode(), builder.build())
  }

  private fun buildForegroundServiceNotification(
    inCall: Boolean,
    peerId: String?,
    connectionState: ServiceConnectionState
  ): Notification {
    val notificationIntent = Intent(this, MainActivity::class.java)
    val pendingIntent = PendingIntent.getActivity(
      this,
      0,
      notificationIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
    val builder = NotificationCompat.Builder(this, FOREGROUND_CHANNEL_ID)
      .setContentTitle(getString(R.string.app_name))
      .setSmallIcon(R.drawable.ic_status_standby)
      .setContentIntent(pendingIntent)

    if (inCall) {
      val hangupIntent = Intent(this, CallService::class.java).apply { action = ACTION_HANGUP }
      val hangupPendingIntent = PendingIntent.getService(
        this,
        3,
        hangupIntent,
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
      )
      builder.setContentText(getString(R.string.service_status_in_call_with, peerId ?: getString(R.string.unknown)))
        .addAction(0, getString(R.string.action_hangup), hangupPendingIntent)
    } else {
      val contentText = when (connectionState) {
        ServiceConnectionState.Online -> getString(R.string.service_status_ready)
        ServiceConnectionState.Establishing -> getString(R.string.service_status_connecting)
        ServiceConnectionState.NoNetwork -> getString(R.string.service_status_no_network)
        ServiceConnectionState.Failure -> getString(R.string.service_status_connection_failed)
        else -> getString(R.string.service_status_background)
      }
      builder.setContentText(contentText)
    }
    return builder.build()
  }

  private fun createNotificationChannels() {
    val foregroundChannel = NotificationChannel(
      FOREGROUND_CHANNEL_ID,
      getString(R.string.notification_channel_foreground_title),
      NotificationManager.IMPORTANCE_MIN
    )
    // IMPORTANT: Changed ID to force reset of channel settings on device
    val callsChannel = NotificationChannel(
      CALLS_CHANNEL_ID,
      getString(R.string.notification_channel_calls_title),
      NotificationManager.IMPORTANCE_HIGH
    ).apply {
      description = getString(R.string.notification_channel_calls_description)
      // Set sound to null to manage manually via CallSoundPlayer and avoid double ringing
      setSound(null, null)
      enableVibration(true)
      lockscreenVisibility = Notification.VISIBILITY_PUBLIC
    }
    val messagesChannel = NotificationChannel(
      MESSAGES_CHANNEL_ID,
      getString(R.string.notification_channel_messages_title),
      NotificationManager.IMPORTANCE_DEFAULT
    )
    notificationManager.createNotificationChannel(foregroundChannel)
    notificationManager.createNotificationChannel(callsChannel)
    notificationManager.createNotificationChannel(messagesChannel)
  }

  private fun acquireWakeLock() {
    if (wakeLock == null) {
      val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
      wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Mercury::CallWakeLock")
      wakeLock?.setReferenceCounted(false)
      wakeLock?.acquire(10 * 60 * 1000L)
    }
  }

  private fun releaseWakeLock() {
    wakeLock?.let { if (it.isHeld) it.release() }
    wakeLock = null
  }

  fun startVoiceCallViaTelecom(targetId: String) {
    placeTelecomCall(targetId, false)
  }

  fun startVideoCallViaTelecom(targetId: String) {
    placeTelecomCall(targetId, true)
  }

  private fun placeTelecomCall(targetId: String, isVideo: Boolean) {
    if (activeConnection != null) return // Already active
    try {
      val extras = Bundle()
      extras.putString(MercuryConnectionService.EXTRA_PEER_ID, targetId)
      val uri = Uri.fromParts(PhoneAccount.SCHEME_SIP, targetId, null)
      val extrasBundle = Bundle()
      extrasBundle.putParcelable(TelecomManager.EXTRA_PHONE_ACCOUNT_HANDLE, phoneAccountHandle)
      extrasBundle.putBundle(TelecomManager.EXTRA_OUTGOING_CALL_EXTRAS, extras)
      if (isVideo) {
        extrasBundle.putInt(
          TelecomManager.EXTRA_START_CALL_WITH_VIDEO_STATE,
          VideoProfile.STATE_BIDIRECTIONAL
        )
      }
      telecomManager.placeCall(uri, extrasBundle)
    } catch (e: SecurityException) {
      Log.e(TAG, "Permission missing for placeCall", e)
    }
  }

  private external fun initEngine(context: Context)
  external fun start()
  private external fun terminate()
  external fun identityExists(): Boolean
  private external fun nativePing(): Boolean
  private external fun nativeRestart()

  companion object {
    var instance: CallService? = null
      private set

    private val _isEngineReady = MutableStateFlow(false)
    val isEngineReady = _isEngineReady.asStateFlow()

    private const val TAG = "CallService"
    private const val FOREGROUND_SERVICE_NOTIFICATION_ID = 101
    const val INCOMING_CALL_NOTIFICATION_ID = 102
    const val MESSAGE_NOTIFICATION_ID_BASE = 1000
    private const val FOREGROUND_CHANNEL_ID = "foreground_service_channel"

    // Bumped version to reset channel settings
    const val CALLS_CHANNEL_ID = "incoming_calls_channel_v3"
    private const val MESSAGES_CHANNEL_ID = "messages_channel"
    private const val ACTION_HANGUP = "hela.eillance.ACTION_HANGUP"
    const val ACTION_ACCEPT_CALL = "hela.eillance.ACTION_ACCEPT_CALL"
    const val ACTION_REJECT_CALL = "hela.eillance.ACTION_REJECT_CALL"
    const val ACTION_SHOW_INCOMING_CALL = "hela.eillance.ACTION_SHOW_INCOMING_CALL"
    private const val ACTION_MARK_READ = "hela.eillance.ACTION_MARK_READ"
    const val EXTRA_PEER_ID = "hela.eillance.EXTRA_PEER_ID"
  }
}
