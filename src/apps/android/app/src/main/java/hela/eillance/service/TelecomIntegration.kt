package hela.eillance.service

import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
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

  private val telecomManager = context.getSystemService(Context.TELECOM_SERVICE) as? TelecomManager
  private val phoneAccountHandle = PhoneAccountHandle(
    ComponentName(context, MercuryConnectionService::class.java),
    TELECOM_ACCOUNT_ID
  )

  fun registerPhoneAccount() {
    val manager = telecomManager ?: return
    if (!context.packageManager.hasSystemFeature(PackageManager.FEATURE_TELECOM)) {
      Log.w(TAG, "Telecom feature not supported on this device")
      return
    }

    val builder =
      PhoneAccount.builder(phoneAccountHandle, context.getString(R.string.telecom_account_name))
        .setCapabilities(PhoneAccount.CAPABILITY_SELF_MANAGED)
        .setShortDescription(context.getString(R.string.telecom_account_description))

    try {
      manager.registerPhoneAccount(builder.build())
    } catch (e: SecurityException) {
      Log.e(TAG, "Failed to register PhoneAccount", e)
    } catch (e: UnsupportedOperationException) {
      Log.e(TAG, "Failed to register PhoneAccount: telecom feature unsupported", e)
    }
  }

  fun processIncomingCall(peerId: String) {
    val manager = telecomManager ?: throw UnsupportedOperationException("TelecomManager not available")
    if (!context.packageManager.hasSystemFeature(PackageManager.FEATURE_TELECOM)) {
      throw UnsupportedOperationException("Telecom feature not supported on this device")
    }

    try {
      val extras = Bundle()
      extras.putString(MercuryConnectionService.EXTRA_PEER_ID, peerId)
      val uri = Uri.fromParts(PhoneAccount.SCHEME_SIP, peerId, null)
      extras.putParcelable(TelecomManager.EXTRA_INCOMING_CALL_ADDRESS, uri)

      val nestedExtras = Bundle()
      nestedExtras.putString(MercuryConnectionService.EXTRA_PEER_ID, peerId)
      extras.putBundle(TelecomManager.EXTRA_INCOMING_CALL_EXTRAS, nestedExtras)

      manager.addNewIncomingCall(phoneAccountHandle, extras)
    } catch (e: Exception) {
      Log.e(TAG, "Failed to add incoming call to Telecom", e)
      throw e // Let caller handle fallback
    }
  }

  fun placeTelecomCall(targetId: String, isVideo: Boolean) {
    val manager = telecomManager ?: return
    if (!context.packageManager.hasSystemFeature(PackageManager.FEATURE_TELECOM)) {
      Log.w(TAG, "Telecom feature not supported on this device")
      return
    }

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
      manager.placeCall(uri, extrasBundle)
    } catch (e: SecurityException) {
      Log.e(TAG, "Permission missing for placeCall", e)
    } catch (e: UnsupportedOperationException) {
      Log.e(TAG, "Telecom placeCall not supported", e)
    }
  }

  companion object {
    private const val TAG = "TelecomIntegration"
    private const val TELECOM_ACCOUNT_ID = "mercury_telecom_account"
  }
}
