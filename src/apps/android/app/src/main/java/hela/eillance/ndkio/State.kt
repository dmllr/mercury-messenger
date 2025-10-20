package hela.eillance.ndkio

import kotlinx.serialization.Serializable

// --- UI State Data Classes ---
// These classes mirror the C++ UIState structure for JSON serialization.

enum class MessageDirection { Sent, Received }

@Serializable
data class HistoryMessage(
    val content: String,
    val timestamp: Long,
    val direction: MessageDirection
)

@Serializable
data class ChatSummary(
    val peer_id: String,
    val last_message: HistoryMessage,
    val unread_count: Int
)

enum class CallDirection { Outgoing, Incoming }
enum class CallStatus { Answered, Missed, Rejected, Busy }

@Serializable
data class CallRecord(
    val peer_id: String,
    val timestamp: Long,
    val duration_seconds: Int,
    val direction: CallDirection,
    val status: CallStatus
)

enum class ContactStatus { pendingApproval, awaitingApproval, confirmed }

@Serializable
data class Contact(
    val peer_id: String,
    val status: ContactStatus
)

@Serializable
data class AppState(
    val my_id: String = "",
    val connection_state: Int = 0, // Maps to ServiceConnectionState
    val contacts: List<Contact> = emptyList(),
    val chats: List<ChatSummary> = emptyList(),
    val calls: List<CallRecord> = emptyList(),
    val selected_contact_id: String? = null,
    val chat_history: List<HistoryMessage> = emptyList(),
    val has_older_messages: Boolean = false
)
