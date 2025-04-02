package hela.eillance.streamer

import android.app.Application
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import hela.eillance.Tune
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.lang.ref.WeakReference

/**
 * ViewModel for managing Streamer application state
 * Acts as a single source of truth for the UI layer
 */
class StreamerViewModel(private val application: Application) : ViewModel() {

  // Service connection state
  private val _connectionState = MutableStateFlow(ServiceConnectionState.Unknown)
  val connectionState: StateFlow<ServiceConnectionState> = _connectionState.asStateFlow()

  // Peer connections state
  private val _peerConnections = MutableStateFlow<Map<String, Map<EventSource, Int>>>(emptyMap())
  val peerConnections: StateFlow<Map<String, Map<EventSource, Int>>> = _peerConnections.asStateFlow()

  // Last ping timestamps for peers
  private val _peerPingTimestamps = MutableStateFlow<Map<String, Long>>(emptyMap())
  val peerPingTimestamps: StateFlow<Map<String, Long>> = _peerPingTimestamps.asStateFlow()

  // Reference to the streamer service - using WeakReference to prevent context leaks
  private var streamerServiceRef: WeakReference<StreamerService>? = null

  // Flag to track if status polling is active
  private var isPollingActive = false

  // Broadcast receiver for state updates
  private var stateUpdateReceiver: BroadcastReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context?, intent: Intent?) {
      intent?.let { processStateUpdateIntent(it) }
    }
  }

  private val localBroadcastManager by lazy {
    LocalBroadcastManager.getInstance(application)
  }

  init {
    Log.d(TAG, "ViewModel initialized")
    registerBroadcastReceiver()
  }

  /**
   * Sets the streamer service instance and starts polling for status updates
   * Uses WeakReference to avoid context leaks
   */
  fun setStreamerService(service: StreamerService?) {
    Log.d(TAG, "Setting streamer service: ${service != null}")
    streamerServiceRef = if (service != null) WeakReference(service) else null

    if (service != null) {
      updateConnectionState()
      startStatusPolling()
    } else {
      _connectionState.value = ServiceConnectionState.Unknown
      stopStatusPolling()
    }
  }

  /**
   * Updates the connection state from the service
   */
  fun updateConnectionState() {
    getService()?.getServiceConnectionState()?.let { state ->
      _connectionState.value = state
    }
  }

  /**
   * Starts periodic polling of the service status
   */
  fun startStatusPolling() {
    if (!isPollingActive) {
      isPollingActive = true
      viewModelScope.launch {
        while (isActive && isPollingActive) {
          updateConnectionState()
          delay(2000) // 2 seconds interval for status updates
        }
      }
    }
  }

  /**
   * Stops periodic polling of the service status
   */
  private fun stopStatusPolling() {
    isPollingActive = false
  }

  /**
   * Registers broadcast receiver for state updates
   */
  private fun registerBroadcastReceiver() {
    val intentFilter = IntentFilter(Tune::BroadcastActionOnStateUpdate.get())
    localBroadcastManager.registerReceiver(stateUpdateReceiver, intentFilter)
    Log.d(TAG, "Broadcast receiver registered")
  }

  /**
   * Processes broadcast events for state updates
   */
  fun processStateUpdateIntent(intent: Intent) {
    if (intent.action != Tune::BroadcastActionOnStateUpdate.get()) return

    val sourceValue = intent.getIntExtra(Tune::BroadcastActionOnStateUpdate_source.get(), 0)
    val peerId = intent.getStringExtra(Tune::BroadcastActionOnStateUpdate_peer_id.get())
    val state = intent.getIntExtra(Tune::BroadcastActionOnStateUpdate_state.get(), 0)
    val source = EventSource.fromInt(sourceValue)

    // Handle system events (connection state)
    if (source == EventSource.System) {
      _connectionState.value = ServiceConnectionState.fromInt(state)
      Log.d(TAG, "Connection state updated: ${_connectionState.value}")
    }
    // Handle peer-specific events
    else if (peerId != null) {
      // Update peer connection state
      _peerConnections.update { currentPeers ->
        val mutablePeers = currentPeers.toMutableMap()
        val peerState = mutablePeers[peerId]?.toMutableMap() ?: mutableMapOf()
        peerState[source] = state
        mutablePeers[peerId] = peerState
        mutablePeers
      }

      // Record ping timestamp
      if (source == EventSource.Ping) {
        _peerPingTimestamps.update { currentTimestamps ->
          val mutableTimestamps = currentTimestamps.toMutableMap()
          mutableTimestamps[peerId] = System.currentTimeMillis()
          mutableTimestamps
        }
        Log.d(TAG, "Ping timestamp updated for peer: $peerId")
      }
    }
  }

  /**
   * Get the current streamer service instance safely through WeakReference
   */
  fun getService(): StreamerService? {
    return streamerServiceRef?.get()
  }

  /**
   * Starts the streaming service
   */
  fun startService() {
    Log.d(TAG, "Starting service")
    getService()?.start()
  }

  /**
   * Stops the streaming service
   */
  fun stopService() {
    Log.d(TAG, "Stopping service")
    getService()?.stop()
  }

  /**
   * Restarts the streaming service
   */
  fun restartService() {
    Log.d(TAG, "Restarting service")
    getService()?.let { service ->
      service.stop()
      service.start()
    }
  }

  override fun onCleared() {
    stopStatusPolling()
    try {
      localBroadcastManager.unregisterReceiver(stateUpdateReceiver)
      Log.d(TAG, "Broadcast receiver unregistered")
    } catch (e: Exception) {
      Log.e(TAG, "Error unregistering broadcast receiver", e)
    }
    streamerServiceRef = null
    super.onCleared()
  }

  /**
   * Factory for creating StreamerViewModel instances
   */
  class Factory(private val context: Context) : ViewModelProvider.Factory {
    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>): T {
      if (modelClass.isAssignableFrom(StreamerViewModel::class.java)) {
        return StreamerViewModel(context.applicationContext as Application) as T
      }
      throw IllegalArgumentException("Unknown ViewModel class")
    }
  }

  companion object {
    private const val TAG = "StreamerViewModel"
  }
}
