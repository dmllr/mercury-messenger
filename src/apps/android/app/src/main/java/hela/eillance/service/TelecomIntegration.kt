package hela.eillance.service

import android.content.ComponentName
import android.content.Context
import android.net.Uri
import android.os.Bundle
import android.telecom.PhoneAccount
import android.telecom.PhoneAccountHandle
import android.telecom.TelecomManager
import android.telecom.VideoProfile
import android.util.Log
import hela.eillance.camera.R
import hela.eillance.telecom.MercuryConnectionService

class TelecomIntegration(private val context: Context) {

  private val telecomManager = context.getSystemService(Context.TELECOM_SERVICE) as TelecomManager
  private val phoneAccountHandle = PhoneAccountHandle(
    ComponentName(context, MercuryConnectionService::class.java),
    TELECOM_ACCOUNT_ID
  )

  fun registerPhoneAccount() {
    val builder =
      PhoneAccount.builder(phoneAccountHandle, context.getString(R.string.telecom_account_name))
        .setCapabilities(PhoneAccount.CAPABILITY_SELF_MANAGED)
        .setShortDescription(context.getString(R.string.telecom_account_description))

    try {
      telecomManager.registerPhoneAccount(builder.build())
    } catch (e: SecurityException) {
      Log.e(TAG, "Failed to register PhoneAccount", e)
    }
  }

  fun processIncomingCall(peerId: String) {
    try {
      val extras = Bundle()
      extras.putString(MercuryConnectionService.EXTRA_PEER_ID, peerId)
      val uri = Uri.fromParts(PhoneAccount.SCHEME_SIP, peerId, null)
      extras.putParcelable(TelecomManager.EXTRA_INCOMING_CALL_ADDRESS, uri)

      val nestedExtras = Bundle()
      nestedExtras.putString(MercuryConnectionService.EXTRA_PEER_ID, peerId)
      extras.putBundle(TelecomManager.EXTRA_INCOMING_CALL_EXTRAS, nestedExtras)

      telecomManager.addNewIncomingCall(phoneAccountHandle, extras)
    } catch (e: Exception) {
      Log.e(TAG, "Failed to add incoming call to Telecom", e)
      throw e // Let caller handle fallback
    }
  }

  fun placeTelecomCall(targetId: String, isVideo: Boolean) {
    try {
      val extras = Bundle()
      extras.putString(MercuryConnectionService.EXTRA_PEER_ID, targetId)
      val uri = Uri.fromParts(PhoneAccount.SCHEME_SIP, targetId, null)
      val extrasBundle = Bundle()
      extrasBundle.putParcelable(TelecomManager.EXTRA_PHONE_ACCOUNT_HANDLE, phoneAccountHandle)
      extrasBundle.putBundle(TelecomManager.EXTRA_OUTGOING_CALL_EXTRAS, extras)
      if (isVideo) {
        extrasBundle.putInt(
          TelecomManager.EXTRA_START_CALL_WITH_VIDEO_STATE,
          VideoProfile.STATE_BIDIRECTIONAL
        )
      }
      telecomManager.placeCall(uri, extrasBundle)
    } catch (e: SecurityException) {
      Log.e(TAG, "Permission missing for placeCall", e)
    }
  }

  companion object {
    private const val TAG = "TelecomIntegration"
    private const val TELECOM_ACCOUNT_ID = "mercury_telecom_account"
  }
}
