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
    val oldStatuses = oldContacts.associate { it.peerId to it.status }
    val oldTimestamps = oldContacts.associate { it.peerId to it.lastActivityTimestamp }

    newContacts.forEach { newContact ->
      oldUnreadCounts[newContact.peerId] ?: 0
      val oldStatus = oldStatuses[newContact.peerId]
      val oldTimestamp = oldTimestamps[newContact.peerId] ?: 0L
      val displayName = newContact.name.ifEmpty { newContact.peerId.take(8) }

      // Contact Request Notification
      val isContactRequest = newContact.status == hela.eillance.ndkio.ContactStatus.awaitingApproval && oldStatus != hela.eillance.ndkio.ContactStatus.awaitingApproval
      if (isContactRequest) {
        notifications.showContactRequestNotification(newContact.peerId, displayName)
      }

      // New Message / Missed Call Notification
      if (newContact.lastActivityTimestamp > oldTimestamp) {
        val unreadMessages = contactsVM?.getUnreadMessages(newContact.peerId) ?: emptyList()
        val newMessages = unreadMessages.filter { it.timestamp > oldTimestamp }

        val missedCalls = newMessages.filter { it.callStatus == hela.eillance.ndkio.CallStatus.Missed }
        val regularMessages = newMessages.filter { 
          it.callStatus != hela.eillance.ndkio.CallStatus.Missed && 
          it.kind != hela.eillance.ndkio.HistoryEntryKind.SystemMessage 
        }

        if (missedCalls.isNotEmpty()) {
          notifications.showMissedCallNotification(newContact.peerId, displayName)
        }

        if (regularMessages.isNotEmpty() && !MercuryApplication.isAppInForeground) {
          val prefs = service.getSharedPreferences("mercury_prefs", Context.MODE_PRIVATE)
          val showContent = prefs.getBoolean("show_notifications_content", true)
          // For the notification, we typically want to pass ALL unread regular messages so they are bundled.
          val allUnreadRegular = unreadMessages.filter {
            it.callStatus != hela.eillance.ndkio.CallStatus.Missed &&
            it.kind != hela.eillance.ndkio.HistoryEntryKind.SystemMessage
          }
          notifications.showNewMessageNotification(
            newContact.peerId,
            displayName,
            allUnreadRegular,
            showContent
          )
        }
      }
    }
  }

  fun markAllAsRead(peerId: String) {
    contactsVM?.markAllAsRead(peerId)
  }
}
