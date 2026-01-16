package hela.eillance.viewmodels

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import hela.eillance.ndkio.Contact
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class ContactsViewModel(application: Application) : AndroidViewModel(application), NativeContactsObserver {

  private var nativePtr: Long = 0
  private val _contacts = MutableStateFlow<List<Contact>>(emptyList())
  val contacts = _contacts.asStateFlow()

  private val _showAddContactDialog = MutableStateFlow(false)
  val showAddContactDialog = _showAddContactDialog.asStateFlow()

  private val _showContactProfileDialog = MutableStateFlow<String?>(null)
  val showContactProfileDialog = _showContactProfileDialog.asStateFlow()

  init {
    nativePtr = nativeCreate(this)
    if (nativePtr == 0L) {
      Log.e("ContactsViewModel", "Failed to create native ContactsViewModel.")
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  // --- Observer Callback called from JNI ---
  override fun onContactsUpdated(contacts: List<Contact>) {
    _contacts.value = contacts
  }

  // --- Actions ---

  fun onAddContactClicked() {
    _showAddContactDialog.value = true
  }

  fun onAddContactDialogDismiss() {
    _showAddContactDialog.value = false
  }

  fun addContact(peerId: String, token: String? = null) {
    if (nativePtr != 0L) nativeAddContact(nativePtr, peerId, token)
    onAddContactDialogDismiss()
  }

  fun onShowContactProfile(peerId: String) {
    _showContactProfileDialog.value = peerId
  }

  fun onContactProfileDialogDismiss() {
    _showContactProfileDialog.value = null
  }

  fun blockContact(peerId: String) {
    if (nativePtr != 0L) nativeBlockContact(nativePtr, peerId)
    if (_showContactProfileDialog.value == peerId) {
      _showContactProfileDialog.value = null
    }
  }

  fun deleteContact(peerId: String) {
    if (nativePtr != 0L) nativeDeleteContact(nativePtr, peerId)
  }

  fun unblockContact(peerId: String) {
    if (nativePtr != 0L) nativeUnblockContact(nativePtr, peerId)
  }

  fun acceptContact(peerId: String) {
    if (nativePtr != 0L) nativeAcceptContact(nativePtr, peerId)
  }

  fun setContactDisplayNameOverride(peerId: String, name: String) {
    if (nativePtr != 0L) nativeSetDisplayName(nativePtr, peerId, name)
  }

  fun resetContactDisplayNameOverride(peerId: String) {
    if (nativePtr != 0L) nativeResetDisplayName(nativePtr, peerId)
  }

  fun generateContactToken(): String? {
    return if (nativePtr != 0L) nativeGenerateToken(nativePtr) else null
  }

  fun sendFeedback() {
    val feedbackId = "ba5fc552e3e16a7604e3ec74ad65370ba1b36345ffcfe7d726e217e85295b382"
    addContact(feedbackId)
  }

  // --- JNI Definitions ---
  private external fun nativeCreate(observer: NativeContactsObserver): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeAddContact(ptr: Long, peerId: String, token: String?)
  private external fun nativeAcceptContact(ptr: Long, peerId: String)
  private external fun nativeBlockContact(ptr: Long, peerId: String)
  private external fun nativeDeleteContact(ptr: Long, peerId: String)
  private external fun nativeUnblockContact(ptr: Long, peerId: String)
  private external fun nativeSetDisplayName(ptr: Long, peerId: String, name: String)
  private external fun nativeResetDisplayName(ptr: Long, peerId: String)
  private external fun nativeGenerateToken(ptr: Long): String
}
