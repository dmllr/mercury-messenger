package hela.eillance.viewmodels

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.MessengerService
import hela.eillance.camera.R
import hela.eillance.events.NativeEventBus
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.GlobalSearchResult
import hela.eillance.ndkio.HistoryEntry
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class ContactsViewModel(application: Application) : AndroidViewModel(application) {

  private var nativePtr: Long = 0
  private val _contacts = MutableStateFlow<List<Contact>>(emptyList())
  val contacts = _contacts.asStateFlow()

  private val _isLoading = MutableStateFlow(true)
  val isLoading = _isLoading.asStateFlow()

  private val _showAddContactDialog = MutableStateFlow(false)
  val showAddContactDialog = _showAddContactDialog.asStateFlow()

  private val _showContactProfileDialog = MutableStateFlow<String?>(null)
  val showContactProfileDialog = _showContactProfileDialog.asStateFlow()

  private val _errorMessage = MutableSharedFlow<String>()
  val errorMessage = _errorMessage.asSharedFlow()

  private val _searchResults = MutableStateFlow<List<GlobalSearchResult>>(emptyList())
  val searchResults = _searchResults.asStateFlow()

  init {
    nativePtr = nativeCreate()
    if (nativePtr == 0L) {
      Log.e("ContactsViewModel", "Failed to create native ContactsViewModel.")
      _isLoading.value = false
    } else {
      // Ensure the engine is running. On a fresh install, the service might have
      // initialized before identity creation, so start() wasn't called yet.
      // Starting the engine now ensures the repository loads and notifies observers
      // via the onContactsUpdated callback, clearing the loading state.
      viewModelScope.launch {
        NativeEventBus.contacts.collect {
          _contacts.value = it
          _isLoading.value = false
        }
      }

      MessengerService.instance?.start()
    }

    // Listen for global errors reported by CallService (e.g., Transport parsing errors)
    viewModelScope.launch {
      MessengerService.instance?.errorEvents?.collect { error ->
        _errorMessage.emit(error)
      }
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  // --- Actions ---

  fun onAddContactClicked() {
    _showAddContactDialog.value = true
  }

  fun onAddContactDialogDismiss() {
    _showAddContactDialog.value = false
  }

  fun addContact(peerId: String, token: String? = null) {
    val success = if (nativePtr != 0L) nativeAddContact(nativePtr, peerId, token) else false
    if (success) {
      onAddContactDialogDismiss()
    } else {
      viewModelScope.launch {
        _errorMessage.emit(getApplication<Application>().getString(R.string.contact_add_error))
      }
    }
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

  fun createGroup(name: String, peerIds: List<String>) {
    if (nativePtr != 0L) {
      nativeCreateGroup(nativePtr, name, peerIds.joinToString(","))
    }
  }

  fun getGroupMembers(groupId: String): List<hela.eillance.ndkio.GroupMember> {
    return if (nativePtr != 0L) nativeGetGroupMembers(nativePtr, groupId).toList() else emptyList()
  }

  fun addGroupMember(groupId: String, contactId: String): Boolean {
    return if (nativePtr != 0L) nativeAddGroupMember(nativePtr, groupId, contactId) else false
  }

  fun removeGroupMember(groupId: String, contactId: String): Boolean {
    return if (nativePtr != 0L) nativeRemoveGroupMember(nativePtr, groupId, contactId) else false
  }

  fun leaveGroup(groupId: String) {
    if (nativePtr != 0L) nativeLeaveGroup(nativePtr, groupId)
  }

  fun createChannel(name: String, description: String): String? {
    return if (nativePtr != 0L) nativeCreateChannel(nativePtr, name, description) else null
  }

  fun subscribeToChannel(channelId: String, ownerId: String) {
    if (nativePtr != 0L) nativeSubscribeToChannel(nativePtr, channelId, ownerId)
  }

  fun unsubscribeChannel(channelId: String) {
    if (nativePtr != 0L) nativeUnsubscribeChannel(nativePtr, channelId)
  }

  fun getChannelMembers(channelId: String): List<hela.eillance.ndkio.GroupMember> {
    return if (nativePtr != 0L) nativeGetChannelMembers(nativePtr, channelId).toList() else emptyList()
  }

  fun addChannelMember(channelId: String, contactId: String): Boolean {
    return if (nativePtr != 0L) nativeAddChannelMember(nativePtr, channelId, contactId) else false
  }

  fun removeChannelMember(channelId: String, contactId: String): Boolean {
    return if (nativePtr != 0L) nativeRemoveChannelMember(nativePtr, channelId, contactId) else false
  }

  fun generateContactToken(): String? {
    return if (nativePtr != 0L) nativeGenerateToken(nativePtr) else null
  }

  fun sendFeedback(onContactReady: (String) -> Unit) {
    val feedbackId = "ba5fc552e3e16a7604e3ec74ad65370ba1b36345ffcfe7d726e217e85295b382"
    val exists = _contacts.value.any { it.peerId == feedbackId }
    if (!exists) {
      val success = if (nativePtr != 0L) nativeAddContact(nativePtr, feedbackId, null) else false
      if (!success) {
        viewModelScope.launch {
          _errorMessage.emit(getApplication<Application>().getString(R.string.contact_add_error))
        }
        return
      }
    }
    onContactReady(feedbackId)
  }

  fun getUnreadMessages(peerId: String): List<HistoryEntry> {
    return if (nativePtr != 0L) nativeGetUnreadMessages(nativePtr, peerId) else emptyList()
  }

  fun markAllAsRead(peerId: String) {
    if (nativePtr != 0L) nativeMarkAllAsRead(nativePtr, peerId)
  }

  fun searchAllMessages(query: String) {
    if (query.isBlank()) {
      _searchResults.value = emptyList()
      return
    }
    viewModelScope.launch {
      val results = withContext(Dispatchers.IO) {
        if (nativePtr != 0L) nativeSearchAllMessages(nativePtr, query) else emptyList()
      }
      _searchResults.value = results
    }
  }

  // --- JNI Definitions ---
  private external fun nativeCreate(): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeAddContact(ptr: Long, peerId: String, token: String?): Boolean
  private external fun nativeAcceptContact(ptr: Long, peerId: String)
  private external fun nativeBlockContact(ptr: Long, peerId: String)
  private external fun nativeDeleteContact(ptr: Long, peerId: String)
  private external fun nativeUnblockContact(ptr: Long, peerId: String)
  private external fun nativeSetDisplayName(ptr: Long, peerId: String, name: String)
  private external fun nativeResetDisplayName(ptr: Long, peerId: String)
  private external fun nativeCreateGroup(ptr: Long, name: String, peerIdsCsv: String): String
  private external fun nativeGenerateToken(ptr: Long): String
  private external fun nativeGetUnreadMessages(ptr: Long, peerId: String): List<HistoryEntry>
  private external fun nativeMarkAllAsRead(ptr: Long, peerId: String)
  private external fun nativeSearchAllMessages(ptr: Long, query: String): List<GlobalSearchResult>

  private external fun nativeGetGroupMembers(ptr: Long, groupId: String): Array<hela.eillance.ndkio.GroupMember>
  private external fun nativeAddGroupMember(ptr: Long, groupId: String, contactId: String): Boolean
  private external fun nativeRemoveGroupMember(ptr: Long, groupId: String, contactId: String): Boolean
  private external fun nativeLeaveGroup(ptr: Long, groupId: String)

  private external fun nativeCreateChannel(ptr: Long, name: String, desc: String): String
  private external fun nativeSubscribeToChannel(ptr: Long, channelId: String, ownerId: String)
  private external fun nativeUnsubscribeChannel(ptr: Long, channelId: String)
  private external fun nativeGetChannelMembers(ptr: Long, channelId: String): Array<hela.eillance.ndkio.GroupMember>
  private external fun nativeAddChannelMember(ptr: Long, channelId: String, contactId: String): Boolean
  private external fun nativeRemoveChannelMember(ptr: Long, channelId: String, contactId: String): Boolean
}
