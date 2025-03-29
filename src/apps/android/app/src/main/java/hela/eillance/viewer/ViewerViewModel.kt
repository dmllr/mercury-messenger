package hela.eillance.viewer

import android.app.Application
import android.util.Log
import android.view.TextureView
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.Peer
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.ndkio.Profile
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class ViewerViewModel(application: Application) : AndroidViewModel(application) {
    var selectedPeer by mutableStateOf<Peer?>(null)
        private set

    private val peerStore = PeerStore(application.applicationContext, viewModelScope)

    private val _livePeers = MutableStateFlow<Map<String, Peer>>(emptyMap())

    val displayedPeers: StateFlow<List<StoredPeer>> =
        combine(peerStore.storedPeers, _livePeers) { stored, live ->
            val updatedStoredPeers = stored.toMutableMap()

            // update status and details based on live peers
            live.values.forEach { livePeer ->
                val existingStored = updatedStoredPeers[livePeer.id]
                if (existingStored != null) {
                    // Peer exists in store, update its status to online and refresh details
                    updatedStoredPeers[livePeer.id] = existingStored.copy(
                        isOnline = true,
                        name = livePeer.name,
                        profile = livePeer.profile
                    )
                } else {
                    // Peer is live but not in store, add it as online
                    updatedStoredPeers[livePeer.id] = StoredPeer(
                        id = livePeer.id,
                        name = livePeer.name,
                        profile = livePeer.profile,
                        isOnline = true,
                        previewImagePath = null
                    )
                }
            }

            // mark stored peers that are *not* in the live list as offline
            stored.keys.forEach { storedPeerId ->
                if (!live.containsKey(storedPeerId) && updatedStoredPeers[storedPeerId]?.isOnline == true) {
                    updatedStoredPeers[storedPeerId] = updatedStoredPeers[storedPeerId]!!.copy(isOnline = false)
                }
            }

            if (updatedStoredPeers != stored) {
                viewModelScope.launch { peerStore.updateStoredPeersMap(updatedStoredPeers) }
            }

            updatedStoredPeers.values.sortedBy { it.name }
        }.stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(5000), // Keep active 5s after last subscriber
            initialValue = emptyList()
        )

    private val _connectionStatus = MutableLiveData<Int>(ServiceConnectionState.Unknown.ordinal)
    val connectionStatus: LiveData<Int> = _connectionStatus

    private val _peerStatus = MutableLiveData<Pair<EventSource, Int>>()
    val peerStatus: LiveData<Pair<EventSource, Int>> = _peerStatus

    private val _streamInfo = MutableLiveData<Triple<Int, Int, Int>>() // width, height, rotation
    val streamInfo: LiveData<Triple<Int, Int, Int>> = _streamInfo
    val streamWidth: Int
        get() = _streamInfo.value?.first ?: 0
    val streamHeight: Int
        get() = _streamInfo.value?.second ?: 0
    val streamRotation: Int
        get() = _streamInfo.value?.third ?: 0

    private val _pingEvent = MutableLiveData<Long>()
    val pingEvent: LiveData<Long> = _pingEvent

    private val _deviceProfile = MutableLiveData<Profile>()
    val deviceProfile: LiveData<Profile> = _deviceProfile // Keep for StreamScreen for now

    fun selectPeerById(peerId: String) {
        // Find the full Peer object (from live or stored data) to navigate
        val peer = displayedPeers.value.find { it.id == peerId }
            ?: _livePeers.value[peerId]?.let { // Fallback to live if somehow not in displayed yet
                StoredPeer(it.id, it.name, it.profile, null, true)
            }

        if (peer != null) {
            // reconstruct if needed
            selectedPeer = Peer(peer.id, peer.name, peer.profile)
            Log.d("ViewModel", "Selected peer: ${peer.id}")
        } else {
            Log.w("ViewModel", "Could not find peer with ID: $peerId to select.")
            selectedPeer = null
        }
    }

    fun updatePeerList(peers: List<Peer>) {
        _livePeers.value = peers.associateBy { it.id }
    }


    fun updatePeerStatus(peerId: String, source: EventSource, status: Int) {
        viewModelScope.launch {
            if (source == EventSource.System) {
                _connectionStatus.postValue(status)
                // If service disconnects or fails, mark all peers offline in store
                val serviceState = ServiceConnectionState.fromInt(status)
                if (serviceState != ServiceConnectionState.Online) {
                    Log.d("ViewModel", "Service not online ($serviceState), marking all peers offline.")
                    peerStore.updateAllPeersOffline()
                    _livePeers.value = emptyMap() // Clear live peers if service is down
                }
            } else {
                // Update status in the persistent store
                val connectionState = if (source == EventSource.PeerConnectionState) PeerConnectionState.fromInt(status) else null
                val isNowOnline = connectionState == PeerConnectionState.Connected

                // Update store regardless of selection
                peerStore.updatePeerStatus(peerId, isNowOnline)

                // If this status update is for the *selected* peer, update UI LiveData
                if (selectedPeer?.id == peerId) {
                    _peerStatus.postValue(Pair(source, status))
                    if (source == EventSource.Ping) {
                        _pingEvent.postValue(System.currentTimeMillis())
                    }
                }
            }
        }
    }

    fun updateStreamInfo(width: Int, height: Int, rotation: Int) {
        _streamInfo.postValue(Triple(width, height, rotation))
    }

    fun updateDeviceProfile(profile: Profile) {
        selectedPeer?.id?.let { peerId ->
            viewModelScope.launch {
                peerStore.updatePeerProfile(peerId, profile)
            }
        }
        _deviceProfile.postValue(profile)
    }

    fun captureAndStorePreview(peerId: String, textureView: TextureView?) {
        viewModelScope.launch {
            textureView?.let { view ->
                try {
                    val originalBitmap = view.bitmap ?: run {
                        Log.w("ViewModel", "Cannot capture preview, bitmap is null for $peerId")
                        return@launch
                    }

                    // use the current transformation matrix from the TextureView
                    val viewTransform = android.graphics.Matrix()
                    view.getTransform(viewTransform)

                    val transformedBitmap = android.graphics.Bitmap.createBitmap(
                        originalBitmap,
                        0,
                        0,
                        originalBitmap.width,
                        originalBitmap.height,
                        viewTransform,
                        true
                    )

                    val isRotated90Degrees = streamRotation == 90 || streamRotation == 270

                    if (isRotated90Degrees) {
                        val correctionMatrix = android.graphics.Matrix()

                        val correctedBitmap = android.graphics.Bitmap.createBitmap(
                            transformedBitmap,
                            0,
                            0,
                            transformedBitmap.width,
                            transformedBitmap.height,
                            correctionMatrix,
                            true
                        )

                        peerStore.savePreviewImage(peerId, correctedBitmap)
                        transformedBitmap.recycle()
                    } else {
                        peerStore.savePreviewImage(peerId, transformedBitmap)
                    }
                    originalBitmap.recycle()
                } catch (e: Exception) {
                }
            }
        }
    }

    fun removePeer(peerId: String) {
        viewModelScope.launch {
            peerStore.removePeer(peerId)
            // If the removed peer was selected, deselect it
            if (selectedPeer?.id == peerId) {
                selectedPeer = null
            }
            // If the removed peer was in the live list
            _livePeers.update { it - peerId }
            Log.d("ViewModel", "Requested removal of peer: $peerId")
        }
    }

    suspend fun PeerStore.updateStoredPeersMap(newMap: Map<String, StoredPeer>) {
        withContext(Dispatchers.IO) {
            _storedPeers.value = newMap
            savePeers()
        }
    }

    suspend fun PeerStore.updatePeerProfile(peerId: String, newProfile: Profile) {
        withContext(Dispatchers.IO) {
            _storedPeers.update { currentMap ->
                val peer = currentMap[peerId]
                if (peer != null && peer.profile != newProfile) {
                    val newMap = currentMap + (peerId to peer.copy(profile = newProfile))
                    savePeers()
                    newMap
                } else {
                    currentMap // no change needed
                }
            }
        }
    }
}
