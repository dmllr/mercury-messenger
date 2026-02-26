package hela.eillance.events

import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow

object NativeEventBus {
  val contacts = MutableStateFlow<List<Contact>>(emptyList())
  val profile = MutableStateFlow(MyProfileInfo())
  val callState = MutableStateFlow(CallState())
  val connectionState = MutableStateFlow(ServiceConnectionState.Unknown)

  val historyUpdates = MutableSharedFlow<Pair<String, HistoryPage>>(extraBufferCapacity = 10)
  val transferProgress = MutableSharedFlow<Triple<String, String, Float>>(extraBufferCapacity = 100)

  class SettingsWrapper(
    val signalingUrl: String?, val timeout: Int, val customStun: String?,
    val customTurn: String?, val customTurnUser: String?, val customTurnPass: String?,
    val playSoundOnSend: Boolean, val playSoundOnReceive: Boolean
  )

  val settings = MutableStateFlow<SettingsWrapper?>(null)

  @JvmStatic
  fun onContactsUpdated(c: List<Contact>) {
    contacts.value = c
  }

  @JvmStatic
  fun onHistoryUpdated(peerId: String, page: HistoryPage) {
    historyUpdates.tryEmit(peerId to page)
  }

  @JvmStatic
  fun onTransferProgress(peerId: String, attId: String, p: Float) {
    transferProgress.tryEmit(Triple(peerId, attId, p))
  }

  @JvmStatic
  fun onProfileUpdated(p: MyProfileInfo) {
    profile.value = p
  }

  @JvmStatic
  fun onCallStateChanged(s: CallState) {
    callState.value = s
  }

  @JvmStatic
  fun onConnectionStateChanged(s: Int) {
    connectionState.value = ServiceConnectionState.fromInt(s)
  }

  @JvmStatic
  fun onSettingsUpdated(
    url: String?, t: Int, stun: String?, turn: String?,
    tUser: String?, tPass: String?, sndS: Boolean, sndR: Boolean
  ) {
    settings.value = SettingsWrapper(url, t, stun, turn, tUser, tPass, sndS, sndR)
  }
}
