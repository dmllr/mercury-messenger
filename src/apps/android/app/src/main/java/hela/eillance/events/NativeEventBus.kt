package hela.eillance.events

import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.DraftAttachment
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.viewmodels.NetworkStats
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow

object NativeEventBus {
  val contacts = MutableStateFlow<List<Contact>>(emptyList())
  val profile = MutableStateFlow(MyProfileInfo())
  val callState = MutableStateFlow(CallState())
  val connectionState = MutableStateFlow(ServiceConnectionState.Unknown)
  val permissionBanner = MutableStateFlow<Int>(0)
  val networkStats = MutableStateFlow(NetworkStats())

  val historyUpdates = MutableSharedFlow<Pair<String, HistoryPage>>(extraBufferCapacity = 10)
  val draftsUpdates = MutableSharedFlow<Pair<String, List<DraftAttachment>>>(extraBufferCapacity = 10)
  val transferProgress = MutableSharedFlow<Triple<String, String, Float>>(extraBufferCapacity = 100)

  // Notification Triggers
  val notifyNewMessage = MutableSharedFlow<Triple<String, String, List<HistoryEntry>>>(extraBufferCapacity = 10)
  val notifyMissedCall = MutableSharedFlow<Pair<String, String>>(extraBufferCapacity = 10)
  val notifyContactRequest = MutableSharedFlow<Pair<String, String>>(extraBufferCapacity = 10)

  // Hardware / OS Orchestration Commands
  val cmdProcessIncomingTelecom = MutableSharedFlow<String>(extraBufferCapacity = 10)
  val cmdProcessOutgoingTelecom = MutableSharedFlow<Pair<String, Boolean>>(extraBufferCapacity = 10)
  val cmdDisconnectTelecom = MutableSharedFlow<String>(extraBufferCapacity = 10)
  val cmdSetTelecomActive = MutableSharedFlow<String>(extraBufferCapacity = 10)
  val cmdSetAudioMode = MutableSharedFlow<Int>(extraBufferCapacity = 10)
  val cmdClearCommunicationDevice = MutableSharedFlow<Unit>(extraBufferCapacity = 10)
  val cmdPlaySound = MutableSharedFlow<Int>(extraBufferCapacity = 10)
  val cmdStopSound = MutableSharedFlow<Unit>(extraBufferCapacity = 10)
  val cmdSetMicrophoneMute = MutableSharedFlow<Boolean>(extraBufferCapacity = 10)
  val cmdSetSpeakerphone = MutableSharedFlow<Boolean>(extraBufferCapacity = 10)

  val peerVersionMismatch = MutableSharedFlow<Pair<String, Boolean>>(extraBufferCapacity = 10)

  class SettingsWrapper(
    val bootstrapNodeUrls: String?, val appendBootstrapNodes: Boolean, val timeout: Int,
    val customTurn: String?, val customTurnUser: String?, val customTurnPass: String?,
    val useCustomTurnOnly: Boolean, val playSoundOnSend: Boolean, val playSoundOnReceive: Boolean,
    val showNotificationContent: Boolean
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
  fun onDraftsUpdated(peerId: String, drafts: List<DraftAttachment>) {
    draftsUpdates.tryEmit(peerId to drafts)
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
    urls: String?, appendUrls: Boolean, t: Int, turn: String?,
    tUser: String?, tPass: String?, turnOnly: Boolean, sndS: Boolean, sndR: Boolean, showNotifContent: Boolean
  ) {
    settings.value = SettingsWrapper(urls, appendUrls, t, turn, tUser, tPass, turnOnly, sndS, sndR, showNotifContent)
  }

  @JvmStatic
  fun onPermissionBannerChanged(type: Int) {
    permissionBanner.value = type
  }

  @JvmStatic
  fun onNetworkStatsUpdated(connectedPeers: Int, bootstrapNodes: Int, dhtPeers: Int, deviceRole: String) {
    android.util.Log.d("NativeEventBus", "Received network stats from C++: peers=$connectedPeers, role=$deviceRole")
    networkStats.value = NetworkStats(
      connectedPeers = connectedPeers,
      bootstrapNodesAlive = bootstrapNodes,
      dhtPeers = dhtPeers,
      deviceRole = deviceRole,
      lastUpdate = System.currentTimeMillis()
    )
  }

  @JvmStatic
  fun onNotifyNewMessage(peerId: String, contactName: String, messages: List<HistoryEntry>) {
    notifyNewMessage.tryEmit(Triple(peerId, contactName, messages))
  }

  @JvmStatic
  fun onNotifyMissedCall(peerId: String, contactName: String) {
    notifyMissedCall.tryEmit(peerId to contactName)
  }

  @JvmStatic
  fun onNotifyContactRequest(peerId: String, contactName: String) {
    notifyContactRequest.tryEmit(peerId to contactName)
  }

  @JvmStatic
  fun onCmdProcessIncomingTelecom(peerId: String) {
    cmdProcessIncomingTelecom.tryEmit(peerId)
  }

  @JvmStatic
  fun onCmdProcessOutgoingTelecom(peerId: String, isVideo: Boolean) {
    cmdProcessOutgoingTelecom.tryEmit(peerId to isVideo)
  }

  @JvmStatic
  fun onCmdDisconnectTelecom(peerId: String) {
    cmdDisconnectTelecom.tryEmit(peerId)
  }

  @JvmStatic
  fun onCmdSetTelecomActive(peerId: String) {
    cmdSetTelecomActive.tryEmit(peerId)
  }

  @JvmStatic
  fun onCmdSetAudioMode(mode: Int) {
    cmdSetAudioMode.tryEmit(mode)
  }

  @JvmStatic
  fun onCmdClearCommunicationDevice() {
    cmdClearCommunicationDevice.tryEmit(Unit)
  }

  @JvmStatic
  fun onCmdPlaySound(type: Int) {
    cmdPlaySound.tryEmit(type)
  }

  @JvmStatic
  fun onCmdStopSound() {
    cmdStopSound.tryEmit(Unit)
  }

  @JvmStatic
  fun onCmdSetMicrophoneMute(isMuted: Boolean) {
    cmdSetMicrophoneMute.tryEmit(isMuted)
  }

  @JvmStatic
  fun onCmdSetSpeakerphone(isSpeakerOn: Boolean) {
    cmdSetSpeakerphone.tryEmit(isSpeakerOn)
  }

  @JvmStatic
  fun onPeerVersionMismatch(peerId: String, weAreOutdated: Boolean) {
    peerVersionMismatch.tryEmit(peerId to weAreOutdated)
  }
}
