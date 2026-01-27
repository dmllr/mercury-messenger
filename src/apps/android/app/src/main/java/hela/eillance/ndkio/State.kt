package hela.eillance.ndkio

import kotlinx.serialization.Serializable

enum class MessageDirection(val value: Int) {
  Sent(0), Received(1);
  companion object { fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: Sent }
}

enum class CallDirection(val value: Int) {
  Outgoing(0), Incoming(1);
  companion object { fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: Outgoing }
}

enum class CallStatus(val value: Int) {
  Answered(0), Missed(1), Rejected(2), Busy(3);
  companion object { fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: Answered }
}

enum class MessageStatus(val value: Int) {
  None(0), Sending(1), Sent(2), Delivered(3), Read(4);
  companion object { fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: None }
}

enum class HistoryEntryKind(val value: Int) {
  TextMessage(0), VoiceCall(1), VideoCall(2), SystemMessage(3);
  companion object { fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: TextMessage }
}

enum class ContactStatus(val value: Int) {
  pendingApproval(0), awaitingApproval(1), confirmed(2), rejected(3);
  companion object { fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: pendingApproval }
}

@Serializable
enum class AttachmentType(val value: Int) {
  Image(0), Video(1), File(2);
  companion object { fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: File }
}

@Serializable
enum class AttachmentStatus(val value: Int) {
  Pending(0), Transferring(1), Done(2), Failed(3), Canceled(4);
  companion object { fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: Pending }
}

@Serializable
data class Attachment(
  val id: String,
  private val typeValue: Int,
  val path: String,
  val mime: String,
  val size: Long,
  val thumbnail: String?,
  val isDownloaded: Boolean,
  private val statusValue: Int = AttachmentStatus.Pending.value
) {
  val type: AttachmentType get() = AttachmentType.fromInt(typeValue)
  val status: AttachmentStatus get() = AttachmentStatus.fromInt(statusValue)
}

data class HistoryEntry(
  val id: Long,
  private val kindValue: Int,
  val timestamp: Long,
  val content: String?,
  private val directionValue: Int,
  val durationSeconds: Int,
  private val callDirectionValue: Int,
  private val callStatusValue: Int,
  private val statusValue: Int,
  val isTruncated: Boolean,
  val attachments: List<Attachment> = emptyList()
) {
  val kind: HistoryEntryKind get() = HistoryEntryKind.fromInt(kindValue)
  val direction: MessageDirection get() = MessageDirection.fromInt(directionValue)
  val status: MessageStatus get() = MessageStatus.fromInt(statusValue)
  val callDirection: CallDirection? get() = if (callDirectionValue != -1) CallDirection.fromInt(callDirectionValue) else null
  val callStatus: CallStatus? get() = if (callStatusValue != -1) CallStatus.fromInt(callStatusValue) else null
}

data class HistoryPage(
  val messages: List<HistoryEntry> = emptyList(),
  val hasOlderMessages: Boolean = false
)

data class Contact(
  val peerId: String,
  val name: String,
  val bio: String?,
  val photo: String?,
  private val statusValue: Int,
  val connectionState: Int,
  val unreadCount: Int,
  val lastMessageSnippet: String?,
  val last_activity_timestamp: Long
) {
  val status: ContactStatus get() = ContactStatus.fromInt(statusValue)
}

data class CallState(
  val isConnecting: Boolean = false,
  val isInCall: Boolean = false,
  val isRinging: Boolean = false,
  private val callDirectionValue: Int = CallDirection.Outgoing.value,
  val peerId: String = "",
  private val callTypeValue: Int = HistoryEntryKind.VoiceCall.value
) {
  val callDirection: CallDirection get() = CallDirection.fromInt(callDirectionValue)
  val callType: HistoryEntryKind get() = HistoryEntryKind.fromInt(callTypeValue)
}

data class MyProfileInfo(
  val myId: String = "",
  val identityExists: Boolean = true,
  val myDisplayName: String? = null,
  val myBio: String? = null,
  val myPhoto: String? = null,
)
