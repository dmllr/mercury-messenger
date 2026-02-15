package hela.eillance.service

import android.app.Application
import android.content.Context
import hela.eillance.MercuryApplication
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.Contact
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

  fun setup(app: Application) {
    contactsVM = ContactsViewModel(app)
    callVM = CallViewModel(app)
    settingsVM = SettingsViewModel(app)

    // Link CallVM to Handler
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

          // Trigger incoming call logic via notifications if needed,
          // though typically CallService triggers this via state changes logic too.
          val wasRinging = callHandler.getLastCallState()?.isRinging == true
          if (!wasRinging && state.isRinging && state.callDirection == CallDirection.Incoming) {
            // Signal to Service to process incoming call logic (Telecom)
            // This is handled by CallService observing state or CallHandler directly?
            // CallHandler.handleCallStateChange calls logic.
          }
        }
      }
      launch {
        settingsVM?.connectionState?.collect { state ->
          val callState = callHandler.getLastCallState()
          onServiceStatusChanged(
            callState?.isInCall == true,
            callState?.peerId,
            state,
            false // isVideo unknown here, updated elsewhere or assumed false for background
          )
        }
      }
    }
  }

  private fun checkForNewMessages(oldContacts: List<Contact>?, newContacts: List<Contact>) {
    if (oldContacts == null) return
    val oldUnreadCounts = oldContacts.associate { it.peerId to it.unreadCount }
    newContacts.forEach { newContact ->
      val oldUnread = oldUnreadCounts[newContact.peerId] ?: 0
      if (newContact.unreadCount > oldUnread && !MercuryApplication.isAppInForeground) {
        val prefs = service.getSharedPreferences("mercury_prefs", Context.MODE_PRIVATE)
        val showContent = prefs.getBoolean("show_notifications_content", true)
        val displayName = newContact.name.ifEmpty { newContact.peerId.take(8) }
        val unreadMessages = contactsVM?.getUnreadMessages(newContact.peerId) ?: emptyList()
        notifications.showNewMessageNotification(newContact.peerId, displayName, unreadMessages, showContent)
      }
    }
  }

  fun markAllAsRead(peerId: String) {
    contactsVM?.markAllAsRead(peerId)
  }
}
