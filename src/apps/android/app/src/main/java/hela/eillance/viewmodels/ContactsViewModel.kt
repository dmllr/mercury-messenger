package hela.eillance.viewmodels

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import hela.eillance.CallService
import hela.eillance.ndkio.Contact
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class ContactsViewModel(application: Application) : AndroidViewModel(application) {
  private var callService: CallService? = null

  private val _contacts = MutableStateFlow<List<Contact>>(emptyList())
  val contacts = _contacts.asStateFlow()

  private val _showAddContactDialog = MutableStateFlow(false)
  val showAddContactDialog = _showAddContactDialog.asStateFlow()

  private val _showContactProfileDialog = MutableStateFlow<String?>(null)
  val showContactProfileDialog = _showContactProfileDialog.asStateFlow()

  fun setService(service: CallService?) {
    callService = service
  }

  fun refreshContacts() {
    _contacts.value = callService?.nativeGetContacts() ?: emptyList()
  }

  fun onAddContactClicked() {
    _showAddContactDialog.value = true
  }

  fun onAddContactDialogDismiss() {
    _showAddContactDialog.value = false
  }

  fun addContact(peerId: String, token: String? = null) {
    callService?.addContact(peerId, token)
    onAddContactDialogDismiss()
  }

  fun onShowContactProfile(peerId: String) {
    _showContactProfileDialog.value = peerId
  }

  fun onContactProfileDialogDismiss() {
    _showContactProfileDialog.value = null
  }

  fun blockContact(peerId: String) {
    callService?.blockContact(peerId)
    if (_showContactProfileDialog.value == peerId) {
      _showContactProfileDialog.value = null
    }
  }

  fun deleteContact(peerId: String) {
    callService?.deleteContact(peerId)
  }

  fun unblockContact(peerId: String) {
    callService?.unblockContact(peerId)
  }

  fun acceptContact(peerId: String) {
    callService?.acceptContact(peerId)
  }

  fun setContactDisplayNameOverride(peerId: String, name: String) {
    callService?.setContactDisplayNameOverride(peerId, name)
  }

  fun resetContactDisplayNameOverride(peerId: String) {
    callService?.resetContactDisplayNameOverride(peerId)
  }

  fun generateContactToken(): String? {
    return callService?.generateContactToken()
  }

  fun sendFeedback() {
    val feedbackId = "ba5fc552e3e16a7604e3ec74ad65370ba1b36345ffcfe7d726e217e85295b382"
    addContact(feedbackId)
  }

  fun debugEraseAllContacts() = callService?.debugEraseAllContacts()
  fun debugRevokeAllApprovals() = callService?.debugRevokeAllApprovals()
}
