package hela.eillance.viewer

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.Peer
import hela.eillance.ndkio.Profile

class ViewerViewModel : ViewModel() {
    var peers by mutableStateOf(listOf<Peer>())
    var selectedPeer by mutableStateOf<Peer?>(null)

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

    private val _deviceProfile = MutableLiveData<Profile>()
    val deviceProfile: LiveData<Profile> = _deviceProfile

    fun updatePeerStatus(peerId: String, source: EventSource, status: Int) {
        if (selectedPeer?.id == peerId) {
            _peerStatus.postValue(Pair(source, status))
        }
    }

    fun updateStreamInfo(width: Int, height: Int, rotation: Int) {
        _streamInfo.postValue(Triple(width, height, rotation))
    }

    fun updateDeviceProfile(profile: Profile) {
        _deviceProfile.postValue(profile)
    }
}
