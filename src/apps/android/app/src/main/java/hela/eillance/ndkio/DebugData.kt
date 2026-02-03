package hela.eillance.ndkio

data class DebugPendingAction(
    val peerId: String,
    val id: Long,
    val payload: String
)

data class DebugConnectionInfo(
    val peerId: String,
    val state: Int,
    val lastActivitySecAgo: Long,
    val isDataChannelOpen: Boolean,
    val hasMediaConnection: Boolean
) {
    val connectionState: PeerConnectionState get() = PeerConnectionState.fromInt(state)
}

data class DebugUnreadInfo(
    val peerId: String,
    val count: Int,
    val lastMsgTimestamp: Long
)
