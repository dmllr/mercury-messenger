package hela.eillance

import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.os.BatteryManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.annotation.Keep
import hela.eillance.camera.R
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.service.CallHandler
import hela.eillance.service.MercuryNotifications
import hela.eillance.service.TelecomIntegration
import hela.eillance.events.NativeEventBus
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.telecom.MercuryConnection
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.webrtc.WebRtcMediaTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONObject
import org.webrtc.VideoSink
import kotlin.collections.filter

class MessengerService : Service() {

  class CallServiceBinder : android.os.Binder()

  private val binder = CallServiceBinder()
  private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
  private val engineMutex = Mutex()

  private var wakeLock: PowerManager.WakeLock? = null

  private lateinit var notifications: MercuryNotifications
  private lateinit var telecomIntegration: TelecomIntegration
  private lateinit var callHandler: CallHandler

  private val _errorEvents = MutableSharedFlow<String>()
  val errorEvents: SharedFlow<String> = _errorEvents.asSharedFlow()

  private val _isCoreUnresponsive = MutableStateFlow(false)
  val isCoreUnresponsive = _isCoreUnresponsive.asStateFlow()

  var isIncomingUIVisible: Boolean = false

  private var activeNetwork: Network? = null

  private var isUnmetered = false
  private var isCharging = false
  private var lastTurnEligibility: Boolean? = null

  private fun evaluateTurnEligibility() {
    val eligible = isUnmetered && isCharging
    if (lastTurnEligibility != eligible) {
      lastTurnEligibility = eligible
      if (_isEngineReady.value) {
        nativeSetTurnEligibility(eligible)
      }
    }
  }

  private val networkCallback = object : ConnectivityManager.NetworkCallback() {
    override fun onAvailable(network: Network) {
      if (activeNetwork == network) return
      activeNetwork = network

      val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
      val caps = cm.getNetworkCapabilities(network)
      isUnmetered = caps?.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED) == true

      serviceScope.launch {
        if (_isEngineReady.value) nativeUpdateNetworkState(true)
      }
      evaluateTurnEligibility()
    }

    override fun onLost(network: Network) {
      if (activeNetwork == network) {
        activeNetwork = null
        isUnmetered = false
        serviceScope.launch {
          if (_isEngineReady.value) nativeUpdateNetworkState(false)
        }
        evaluateTurnEligibility()
      }
    }

    override fun onCapabilitiesChanged(network: Network, networkCapabilities: NetworkCapabilities) {
      if (activeNetwork == network) {
        val currentlyUnmetered = networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
        if (isUnmetered != currentlyUnmetered) {
          isUnmetered = currentlyUnmetered
          evaluateTurnEligibility()
        }
      }
    }
  }

  private val powerReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context?, intent: Intent?) {
      when (intent?.action) {
        Intent.ACTION_POWER_CONNECTED -> {
          isCharging = true
          evaluateTurnEligibility()
        }
        Intent.ACTION_POWER_DISCONNECTED -> {
          isCharging = false
          evaluateTurnEligibility()
        }
      }
    }
  }

  override fun onBind(intent: Intent?): IBinder = binder

  override fun onCreate() {
    super.onCreate()
    instance = this

    notifications = MercuryNotifications(this)
    telecomIntegration = TelecomIntegration(this)
    callHandler = CallHandler(this, notifications, serviceScope)

    callHandler.init()
    telecomIntegration.registerPhoneAccount()

    val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    activeNetwork = cm.activeNetwork
    val caps = cm.getNetworkCapabilities(activeNetwork)
    isUnmetered = caps?.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED) == true

    val batteryIntent = registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
    val plugged = batteryIntent?.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1) ?: -1
    isCharging = plugged == BatteryManager.BATTERY_PLUGGED_AC || plugged == BatteryManager.BATTERY_PLUGGED_USB || plugged == BatteryManager.BATTERY_PLUGGED_WIRELESS

    cm.registerDefaultNetworkCallback(networkCallback)
    val filter = IntentFilter().apply {
      addAction(Intent.ACTION_POWER_CONNECTED)
      addAction(Intent.ACTION_POWER_DISCONNECTED)
    }
    registerReceiver(powerReceiver, filter)

    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        try {
          initEngine(this@MessengerService)
          if (identityExists()) {
            start()
          }
          with(Dispatchers.Main) {
            setupEventBusObservers()
          }
          _isEngineReady.value = true
          evaluateTurnEligibility()
        } catch (e: Exception) {
          Log.e(TAG, "Failed to initialize engine", e)
        }
      }
    }

    startForegroundService()
  }

  private fun setupEventBusObservers() {
    serviceScope.launch {
      launch {
        NativeEventBus.callState.collect { state ->
          val isActive = state.isInCall || state.isRinging
          updateForegroundServiceNotification(isActive, state.peerId, NativeEventBus.connectionState.value, state.callType == HistoryEntryKind.VideoCall)
        }
      }
      launch {
        NativeEventBus.connectionState.collect { state ->
          val callState = NativeEventBus.callState.value
          val isActive = callState.isInCall || callState.isRinging
          updateForegroundServiceNotification(isActive, callState.peerId.ifEmpty { null }, state, callState.callType == HistoryEntryKind.VideoCall)
        }
      }
      launch {
        NativeEventBus.notifyNewMessage.collect { (peerId, name, messages) ->
          val showContent = NativeEventBus.settings.value?.showNotificationContent ?: true
          notifications.showNewMessageNotification(peerId, name, messages, showContent)
        }
      }
      launch {
        NativeEventBus.notifyMissedCall.collect { (peerId, name) ->
          notifications.showMissedCallNotification(peerId, name)
        }
      }
      launch {
        NativeEventBus.notifyContactRequest.collect { (peerId, name) ->
          notifications.showContactRequestNotification(peerId, name)
        }
      }
      launch { NativeEventBus.cmdProcessIncomingTelecom.collect { callHandler.processIncomingTelecomCall(it) } }
      launch { NativeEventBus.cmdProcessOutgoingTelecom.collect { callHandler.processOutgoingTelecomCall(it.first, it.second) } }
      launch { NativeEventBus.cmdDisconnectTelecom.collect { callHandler.disconnectTelecomCall(it) } }
      launch { NativeEventBus.cmdSetTelecomActive.collect { callHandler.setTelecomCallActive(it) } }
      launch { NativeEventBus.cmdSetAudioMode.collect { callHandler.setAudioMode(it) } }
      launch { NativeEventBus.cmdClearCommunicationDevice.collect { callHandler.clearCommunicationDevice() } }
      launch { NativeEventBus.cmdPlaySound.collect { callHandler.playSound(it) } }
      launch { NativeEventBus.cmdStopSound.collect { callHandler.stopSound() } }
      launch { NativeEventBus.cmdSetMicrophoneMute.collect { callHandler.setMicrophoneMute(it) } }
      launch { NativeEventBus.cmdSetSpeakerphone.collect { callHandler.setSpeakerphoneOn(it) } }
    }
  }

  fun resolveContactInfo(peerId: String): Contact? {
    return NativeEventBus.contacts.value.find { it.peerId == peerId }
  }

  fun restartCore() {
    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        callHandler.stopLocalMedia()
        nativeRestart()
        _isCoreUnresponsive.value = false
        _errorEvents.emit(getString(R.string.core_reset_success))
      }
    }
  }

  @Keep
  fun onNativeCoreCrash() {
    Log.e(TAG, "Native core crash/hang detected by C++ Watchdog!")
    _isCoreUnresponsive.value = true
  }

  // --- Delegation Methods ---

  fun acceptCall() = callHandler.acceptCall()
  fun rejectCall() = callHandler.rejectCall()
  fun hangup() = callHandler.hangup()

  fun onTelecomDisconnect(connection: MercuryConnection) =
    callHandler.onTelecomDisconnect(connection)

  fun setLocalVideoSink(sink: VideoSink?) = callHandler.setLocalVideoSink(sink)
  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) =
    callHandler.setRemoteVideoSink(peerId, sink)

  fun setMicrophoneMute(muted: Boolean) = callHandler.setMicrophoneMute(muted)
  fun setSpeakerphoneOn(on: Boolean) = callHandler.setSpeakerphoneOn(on)

  fun showIncomingCallNotification(peerId: String) =
    notifications.showIncomingCallNotification(peerId)

  // --- Lifecycle & Intent Handling ---

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      ACTION_APP_FOREGROUND -> {
        if (_isEngineReady.value) nativeOnAppForeground()
      }
      ACTION_APP_BACKGROUND -> {
        if (_isEngineReady.value) nativeOnAppBackground()
      }
      ACTION_REJECT_CALL -> rejectCall()
      ACTION_ACCEPT_CALL -> acceptCall()
      ACTION_HANGUP -> hangup()
      ACTION_MARK_READ -> {
        val peerId = intent.getStringExtra(EXTRA_PEER_ID)
        if (peerId != null) {
          serviceScope.launch {
            MessengerService.isEngineReady.first { it }
            nativeMarkAllAsRead(peerId)
            notifications.cancelMessageNotification(peerId)
          }
        }
      }
    }
    return START_STICKY
  }

  override fun onDestroy() {
    instance = null
    _isEngineReady.value = false
    callHandler.stop()

    val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    cm.unregisterNetworkCallback(networkCallback)
    unregisterReceiver(powerReceiver)

    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        terminate()
      }
    }
    releaseWakeLock()
    super.onDestroy()
  }

  // --- Transport Setters ---

  @Suppress("unused")
  fun setWebRtcMediaTransport(transport: WebRtcMediaTransport) {
    callHandler.mediaTransport = transport
  }

  // --- Internal Logic ---

  fun onActivityStopped() {
    val currentState = callHandler.getLastCallState()
    if (currentState != null && currentState.isRinging && currentState.callDirection == CallDirection.Incoming) {
      notifications.showIncomingCallNotification(currentState.peerId)
    }
  }

  fun onConnectionCreated(connection: MercuryConnection, peerId: String) {
    callHandler.onConnectionCreated(connection)
  }

  private fun startForegroundService() {
    val notification =
      notifications.buildForegroundServiceNotification(false, null, ServiceConnectionState.Unknown)
    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        startForeground(
          101, // ID matching NotificationManager constant
          notification,
          ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        )
      } else {
        startForeground(101, notification)
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to start foreground service", e)
    }
  }

  private fun updateForegroundServiceNotification(
    inCall: Boolean,
    peerId: String?,
    connectionState: ServiceConnectionState,
    isVideo: Boolean
  ) {
    notifications.updateForegroundServiceNotification(
      inCall, peerId, connectionState, isVideo
    ) { shouldAcquire ->
      if (shouldAcquire) acquireWakeLock() else releaseWakeLock()
    }
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

  private external fun initEngine(context: Context)
  external fun start()
  private external fun terminate()
  external fun identityExists(): Boolean
  private external fun nativeRestart()
  private external fun nativeOnAppForeground()
  private external fun nativeOnAppBackground()

  external fun nativeAcceptCall()
  external fun nativeRejectCall()
  external fun nativeHangupCall()
  external fun nativeTelecomCallEnded()
  external fun nativeMarkAllAsRead(peerId: String)
  external fun nativeUpdatePlatformPermissions(notif: Boolean, batt: Boolean, calls: Boolean)
  external fun nativeIgnoreBattery()
  external fun nativeIgnoreCalls()
  external fun nativeResetIgnoredPermissions()
  external fun nativeSetTurnEligibility(eligible: Boolean)
  external fun nativeUpdateNetworkState(isOnline: Boolean)

  companion object {
    var instance: MessengerService? = null
      private set

    private val _isEngineReady = MutableStateFlow(false)
    val isEngineReady = _isEngineReady.asStateFlow()

    private const val TAG = "CallService"
    const val INCOMING_CALL_NOTIFICATION_ID = 102
    const val MESSAGE_NOTIFICATION_ID_BASE = 1000
    const val CALLS_CHANNEL_ID = "incoming_calls_channel_v7"
    const val ACTION_HANGUP = "hela.eillance.ACTION_HANGUP"
    const val ACTION_ACCEPT_CALL = "hela.eillance.ACTION_ACCEPT_CALL"
    const val ACTION_REJECT_CALL = "hela.eillance.ACTION_REJECT_CALL"
    const val ACTION_SHOW_INCOMING_CALL = "hela.eillance.ACTION_SHOW_INCOMING_CALL"
    const val ACTION_MARK_READ = "hela.eillance.ACTION_MARK_READ"
    const val ACTION_APP_FOREGROUND = "hela.eillance.ACTION_APP_FOREGROUND"
    const val ACTION_APP_BACKGROUND = "hela.eillance.ACTION_APP_BACKGROUND"
    const val EXTRA_PEER_ID = "hela.eillance.EXTRA_PEER_ID"
  }
}
