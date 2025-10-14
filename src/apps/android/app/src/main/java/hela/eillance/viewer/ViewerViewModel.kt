package hela.eillance.viewer

import android.app.Application
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.Peer
import hela.eillance.ndkio.Profile
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.launch

class ViewerViewModel(application: Application) : AndroidViewModel(application) {
    var selectedPeer by mutableStateOf<Peer?>(null)
        private set

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
    val deviceProfile: LiveData<Profile> = _deviceProfile

    fun selectPeerById(peerId: String) {
        selectedPeer = Peer(id = peerId, name = peerId, profile = emptyList())
        Log.d("ViewModel", "Selected peer for connection: ${'$'}{peerId}")
    }

    fun onPeerListUpdate(peers: List<Peer>) {
        Log.d("ViewModel", "Received available streamers list with ${'$'}{peers.size} peer(s).")
    }


    fun updatePeerStatus(peerId: String, source: EventSource, status: Int) {
        viewModelScope.launch {
            if (source == EventSource.System) {
                _connectionStatus.postValue(status)
            } else {
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
        _deviceProfile.postValue(profile)
    }
}
