package hela.eillance.viewmodels

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import hela.eillance.CallService
import hela.eillance.ndkio.HistoryPage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class ConversationViewModel(application: Application) : AndroidViewModel(application) {
  private var callService: CallService? = null

  private val _interactionHistoryPage = MutableStateFlow(HistoryPage())
  val interactionHistoryPage = _interactionHistoryPage.asStateFlow()

  private val _selectedContactId = MutableStateFlow<String?>(null)
  val selectedContactId = _selectedContactId.asStateFlow()

  fun setService(service: CallService?) {
    callService = service
  }

  fun refreshHistory() {
    _interactionHistoryPage.value = callService?.nativeGetInteractionHistoryPage() ?: HistoryPage()
  }

  fun refreshSelectedContact() {
    _selectedContactId.value = callService?.nativeGetSelectedContactId()
  }

  fun selectContact(peerId: String) {
    callService?.selectContact(peerId)
  }

  fun sendMessage(message: String) {
    if (message.isNotBlank()) {
      callService?.sendMessage(message)
    }
  }

  fun loadOlderMessages() {
    callService?.loadOlderMessages()
  }

  fun loadFullMessage(messageId: Long) {
    callService?.loadFullMessage(messageId)
  }

  fun markMessagesAsRead(messageIds: List<Long>) {
    callService?.markMessagesAsRead(messageIds.toLongArray())
  }

  fun debugEraseAllHistory() = callService?.debugEraseAllHistory()
}
