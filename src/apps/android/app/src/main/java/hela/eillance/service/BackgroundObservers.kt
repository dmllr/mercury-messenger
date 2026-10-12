package hela.eillance.service

import android.app.Application
import android.content.Context
import hela.eillance.MercuryApplication
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.SettingsViewModel
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch

class BackgroundObservers(
  private val service: Context,
  private val scope: CoroutineScope,
  private val notifications: MercuryNotifications,
  private val callHandler: CallHandler,
  private val onServiceStatusChanged: (Boolean, String?, ServiceConnectionState, Boolean) -> Unit
) {

  var contactsVM: ContactsViewModel? = null
  var callVM: CallViewModel? = null
  var settingsVM: SettingsViewModel? = null

  private var lastKnownContacts: List<Contact>? = null
  private var lastConnectionState: ServiceConnectionState = ServiceConnectionState.Unknown

  fun setup(app: Application) {
    contactsVM = ContactsViewModel(app)
    callVM = CallViewModel(app)
    settingsVM = SettingsViewModel(app)

    callHandler.callViewModel = callVM

    scope.launch {
      launch {
        contactsVM?.contacts?.collect { contacts ->
          checkForNewMessages(lastKnownContacts, contacts)
          lastKnownContacts = contacts
        }
      }
      launch {
        callVM?.callState?.collect { state ->
          callHandler.handleCallStateChange(state)

          // Force update foreground notification/wakelock
          // We treat Ringing as "In Call" for the purpose of holding a WakeLock
          val isActive = state.isInCall || state.isRinging
          onServiceStatusChanged(
            isActive,
            state.peerId,
            lastConnectionState,
            state.callType == HistoryEntryKind.VideoCall
          )
        }
      }
      launch {
        settingsVM?.connectionState?.collect { state ->
          lastConnectionState = state
          val callState = callHandler.getLastCallState()
          val isActive = callState?.isInCall == true || callState?.isRinging == true
          onServiceStatusChanged(
            isActive,
            callState?.peerId,
            state,
            callState?.callType == HistoryEntryKind.VideoCall
          )
        }
      }
    }
  }

  fun getContact(peerId: String): Contact? {
    return lastKnownContacts?.find { it.peerId == peerId }
  }

  private fun checkForNewMessages(oldContacts: List<Contact>?, newContacts: List<Contact>) {
    if (oldContacts == null) return
    val oldUnreadCounts = oldContacts.associate { it.peerId to it.unreadCount }
    newContacts.forEach { newContact ->
      val oldUnread = oldUnreadCounts[newContact.peerId] ?: 0
      // Trigger notification refresh if unread count changed (could be new message OR delete/edit of old one)
      // Logic: even if count goes down (read elsewhere), we might want to refresh to remove deleted msg content.
      // But standard path is: new message -> count++ -> notify.
      // For edits/deletes, the count might remain same, but content changes. Core emits EventMessageUpdated.
      if (newContact.unreadCount > oldUnread && !MercuryApplication.isAppInForeground) {
        val prefs = service.getSharedPreferences("mercury_prefs", Context.MODE_PRIVATE)
        val showContent = prefs.getBoolean("show_notifications_content", true)
        val displayName = newContact.name.ifEmpty { newContact.peerId.take(8) }
        val unreadMessages = contactsVM?.getUnreadMessages(newContact.peerId) ?: emptyList()
        notifications.showNewMessageNotification(
          newContact.peerId,
          displayName,
          unreadMessages,
          showContent
        )
      }
    }
  }

  fun markAllAsRead(peerId: String) {
    contactsVM?.markAllAsRead(peerId)
  }
}
