package hela.eillance.service

import android.app.Application
import android.content.Context
import hela.eillance.events.NativeEventBus
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
          lastKnownContacts = contacts
        }
      }
      launch {
        callVM?.callState?.collect { state ->
          callHandler.updateSoundAndNotification(state)

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

      // Explicit Notification Triggers
      launch {
        NativeEventBus.notifyNewMessage.collect { (peerId, _) ->
          val contact = getContact(peerId)
          val displayName = contact?.name?.ifEmpty { peerId.take(8) } ?: peerId.take(8)
          val prefs = service.getSharedPreferences("mercury_prefs", Context.MODE_PRIVATE)
          val showContent = prefs.getBoolean("show_notifications_content", true)

          val unreadMessages = contactsVM?.getUnreadMessages(peerId) ?: emptyList()
          val allUnreadRegular = unreadMessages.filter {
            it.callStatus != hela.eillance.ndkio.CallStatus.Missed &&
            it.kind != hela.eillance.ndkio.HistoryEntryKind.SystemMessage
          }

          notifications.showNewMessageNotification(
            peerId,
            displayName,
            allUnreadRegular,
            showContent
          )
        }
      }
      launch {
        NativeEventBus.notifyMissedCall.collect { peerId ->
          val contact = getContact(peerId)
          val displayName = contact?.name?.ifEmpty { peerId.take(8) } ?: peerId.take(8)
          notifications.showMissedCallNotification(peerId, displayName)
        }
      }
      launch {
        NativeEventBus.notifyContactRequest.collect { peerId ->
          val contact = getContact(peerId)
          val displayName = contact?.name?.ifEmpty { peerId.take(8) } ?: peerId.take(8)
          notifications.showContactRequestNotification(peerId, displayName)
        }
      }

      // Hardware/OS Orchestration Commands
      launch {
        NativeEventBus.cmdProcessIncomingTelecom.collect { peerId ->
          callHandler.processIncomingTelecomCall(peerId)
        }
      }
      launch {
        NativeEventBus.cmdProcessOutgoingTelecom.collect { (peerId, isVideo) ->
          callHandler.processOutgoingTelecomCall(peerId, isVideo)
        }
      }
      launch {
        NativeEventBus.cmdDisconnectTelecom.collect { peerId ->
          callHandler.disconnectTelecomCall(peerId)
        }
      }
      launch {
        NativeEventBus.cmdSetTelecomActive.collect { peerId ->
          callHandler.setTelecomCallActive(peerId)
        }
      }
      launch {
        NativeEventBus.cmdSetAudioMode.collect { mode ->
          callHandler.setAudioMode(mode)
        }
      }
      launch {
        NativeEventBus.cmdClearCommunicationDevice.collect {
          callHandler.clearCommunicationDevice()
        }
      }
      launch {
        NativeEventBus.cmdPlaySound.collect { type ->
          callHandler.playSound(type)
        }
      }
      launch {
        NativeEventBus.cmdStopSound.collect {
          callHandler.stopSound()
        }
      }
    }
  }

  fun getContact(peerId: String): Contact? {
    return lastKnownContacts?.find { it.peerId == peerId }
  }

  fun markAllAsRead(peerId: String) {
    contactsVM?.markAllAsRead(peerId)
  }
}
