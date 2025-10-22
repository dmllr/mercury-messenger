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
data class HistoryMessage(
    val content: String,
    val timestamp: Long,
    val direction: MessageDirection
)

@Serializable
data class ChatSummary(
    @SerialName("peer_id") val peerId: String,
    @SerialName("last_message") val lastMessage: HistoryMessage,
    @SerialName("unread_count") val unreadCount: Int
)

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
data class CallRecord(
    @SerialName("peer_id") val peerId: String,
    val timestamp: Long,
    @SerialName("duration_seconds") val durationSeconds: Int,
    val direction: CallDirection,
    val status: CallStatus
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
    val calls: List<CallRecord> = emptyList(),
    @SerialName("selected_contact_id") val selectedContactId: String? = null,
    @SerialName("chat_history") val chatHistory: List<HistoryMessage> = emptyList(),
    @SerialName("selected_contact_calls") val selectedContactCalls: List<CallRecord> = emptyList(),
    @SerialName("has_older_messages") val hasOlderMessages: Boolean = false
)
