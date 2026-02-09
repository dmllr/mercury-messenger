package hela.eillance.viewmodels

import androidx.annotation.Keep
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.MyProfileInfo

/**
 * These classes are instantiated by Kotlin ViewModels and passed to the JNI layer.
 * The C++ layer keeps a GlobalRef to them and calls the methods defined here.
 * @Keep ensures ProGuard does not rename these methods.
 */

@Keep
interface NativeContactsObserver {
  fun onContactsUpdated(contacts: List<Contact>)
}

@Keep
interface NativeConversationObserver {
  fun onHistoryUpdated(page: HistoryPage)
  fun onTransferProgress(attachmentId: String, progress: Float)
}

@Keep
interface NativeProfileObserver {
  fun onProfileUpdated(profile: MyProfileInfo)
}

@Keep
interface NativeCallObserver {
  fun onCallStateChanged(state: CallState)
}

@Keep
interface NativeSettingsObserver {
  fun onSettingsUpdated(
    signalingUrl: String?,
    timeout: Int,
    customStun: String?,
    customTurn: String?,
    customTurnUser: String?,
    customTurnPass: String?
  )

  fun onConnectionStateChanged(state: Int)
}
