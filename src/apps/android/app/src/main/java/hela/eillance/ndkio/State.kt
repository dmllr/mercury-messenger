package hela.eillance.ndkio

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

// --- UI State Data Classes ---
// These classes mirror the C++ UIState structure for JSON serialization.

@Serializable
enum class MessageDirection {
    @SerialName("sent") Sent,
    @SerialName("received") Received
}

@Serializable
enum class CallDirection {
    @SerialName("outgoing") Outgoing,
    @SerialName("incoming") Incoming
}

@Serializable
enum class CallStatus {
    @SerialName("answered") Answered,
    @SerialName("missed") Missed,
    @SerialName("rejected") Rejected,
    @SerialName("busy") Busy
}

@Serializable
enum class HistoryEntryKind {
    @SerialName("text_message") TextMessage,
    @SerialName("voice_call") VoiceCall,
    @SerialName("video_call") VideoCall,
    @SerialName("system_message") SystemMessage
}

@Serializable
data class HistoryEntry(
    val kind: HistoryEntryKind,
    val timestamp: Long,
    val content: String? = null,
    val direction: MessageDirection,
    @SerialName("duration_seconds") val durationSeconds: Int? = null,
    @SerialName("call_direction") val callDirection: CallDirection? = null,
    @SerialName("call_status") val callStatus: CallStatus? = null,
)

@Serializable
data class ChatSummary(
    @SerialName("peer_id") val peerId: String,
    @SerialName("last_message") val lastMessage: HistoryEntry,
    @SerialName("unread_count") val unreadCount: Int
)

@Serializable
enum class ContactStatus {
    @SerialName("pending_approval") pendingApproval,
    @SerialName("awaiting_approval") awaitingApproval,
    @SerialName("confirmed") confirmed
}

@Serializable
data class Contact(
    @SerialName("peer_id") val peerId: String,
    val status: ContactStatus
)

@Serializable
data class AppState(
    @SerialName("my_id") val myId: String = "",
    @SerialName("connection_state") val connectionState: Int = 0, // Maps to ServiceConnectionState
    val contacts: List<Contact> = emptyList(),
    val chats: List<ChatSummary> = emptyList(),
    @SerialName("selected_contact_id") val selectedContactId: String? = null,
    @SerialName("interaction_history") val interactionHistory: List<HistoryEntry> = emptyList(),
    @SerialName("has_older_messages") val hasOlderMessages: Boolean = false
)
