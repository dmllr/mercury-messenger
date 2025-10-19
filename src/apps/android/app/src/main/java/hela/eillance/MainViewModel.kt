package hela.eillance

import android.app.Application
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.lifecycle.AndroidViewModel
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import hela.eillance.ndkio.Peer
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.io.Serializable

class MainViewModel(application: Application) : AndroidViewModel(application) {
  private var callService: CallService? = null

  private val _peerList = MutableStateFlow<List<Peer>>(emptyList())
  val peerList: StateFlow<List<Peer>> = _peerList.asStateFlow()

  private val _streamInfo = MutableStateFlow<Triple<Int, Int, Int>?>(null)
  val streamInfo: StateFlow<Triple<Int, Int, Int>?> = _streamInfo.asStateFlow()

  private val _showAddContactDialog = MutableStateFlow(false)
  val showAddContactDialog: StateFlow<Boolean> = _showAddContactDialog.asStateFlow()


  private val broadcastReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context?, intent: Intent?) {
      when (intent?.action) {
        CallService.ACTION_PEER_LIST_UPDATE -> {
          val serializablePeers = intent.getSerializableExtra(CallService.EXTRA_PEERS) as? ArrayList<HashMap<String, Serializable>>
          _peerList.update {
            serializablePeers?.map { map ->
              Peer(
                id = map["id"] as String,
                name = map["name"] as String,
                profile = emptyList() // Profile data not passed via broadcast for simplicity
              )
            } ?: emptyList()
          }
        }
        CallService.ACTION_STREAM_INFO -> {
          val width = intent.getIntExtra(CallService.EXTRA_STREAM_WIDTH, 0)
          val height = intent.getIntExtra(CallService.EXTRA_STREAM_HEIGHT, 0)
          val rotation = intent.getIntExtra(CallService.EXTRA_STREAM_ROTATION, 0)
          _streamInfo.value = Triple(width, height, rotation)
        }
      }
    }
  }

  init {
    val filter = IntentFilter().apply {
      addAction(CallService.ACTION_PEER_LIST_UPDATE)
      addAction(CallService.ACTION_STREAM_INFO)
      addAction(Tune.BroadcastActionOnStateUpdate)
    }
    LocalBroadcastManager.getInstance(application).registerReceiver(broadcastReceiver, filter)
  }

  fun setService(service: CallService?) {
    this.callService = service
    refreshPeerList()
  }

  fun refreshPeerList() {
    callService?.requestPeerListUpdate()
  }

  fun onAddContactClicked() {
    _showAddContactDialog.value = true
  }

  fun onAddContactDialogDismiss() {
    _showAddContactDialog.value = false
  }

  fun addContact(peerId: String) {
    callService?.addContact(peerId)
    onAddContactDialogDismiss()
  }


  override fun onCleared() {
    LocalBroadcastManager.getInstance(getApplication()).unregisterReceiver(broadcastReceiver)
    super.onCleared()
  }
}
