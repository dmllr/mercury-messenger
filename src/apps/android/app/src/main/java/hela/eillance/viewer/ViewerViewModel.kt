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

    private val _streamResolution = MutableLiveData<Pair<Int, Int>>()
    val streamResolution: LiveData<Pair<Int, Int>> = _streamResolution

    private val _deviceProfile = MutableLiveData<Profile>()
    val deviceProfile: LiveData<Profile> = _deviceProfile

    fun updatePeerStatus(peerId: String, source: EventSource, status: Int) {
        if (selectedPeer?.id == peerId) {
            _peerStatus.postValue(Pair(source, status))
        }
    }

    fun updateStreamResolution(width: Int, height: Int) {
        _streamResolution.postValue(Pair(width, height))
    }

    fun updateDeviceProfile(profile: Profile) {
        _deviceProfile.postValue(profile)
    }
}
