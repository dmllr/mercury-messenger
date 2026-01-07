package hela.eillance.telecom

import android.telecom.Connection
import android.telecom.ConnectionRequest
import android.telecom.ConnectionService
import android.telecom.PhoneAccountHandle
import android.telecom.TelecomManager
import android.util.Log
import hela.eillance.CallService

class MercuryConnectionService : ConnectionService() {

  override fun onCreateIncomingConnection(
    connectionManagerPhoneAccount: PhoneAccountHandle?,
    request: ConnectionRequest?
  ): Connection {
    return createConnection(request, incoming = true)
  }

  override fun onCreateOutgoingConnection(
    connectionManagerPhoneAccount: PhoneAccountHandle?,
    request: ConnectionRequest?
  ): Connection {
    return createConnection(request, incoming = false)
  }

  override fun onCreateIncomingConnectionFailed(
    connectionManagerPhoneAccount: PhoneAccountHandle?,
    request: ConnectionRequest?
  ) {
    Log.e(TAG, "onCreateIncomingConnectionFailed")
    super.onCreateIncomingConnectionFailed(connectionManagerPhoneAccount, request)
  }

  override fun onCreateOutgoingConnectionFailed(
    connectionManagerPhoneAccount: PhoneAccountHandle?,
    request: ConnectionRequest?
  ) {
    Log.e(TAG, "onCreateOutgoingConnectionFailed")
    super.onCreateOutgoingConnectionFailed(connectionManagerPhoneAccount, request)
  }

  private fun createConnection(request: ConnectionRequest?, incoming: Boolean): Connection {
    // 1. Try to get ID from the address (sip:peerId)
    var peerId: String? = request?.address?.schemeSpecificPart

    // 2. Try explicit extra
    if (peerId == null || peerId.isEmpty()) {
      peerId = request?.extras?.getString(EXTRA_PEER_ID)
    }

    // 3. Try nested extras (some Android versions wrap incoming extras)
    if ((peerId == null || peerId.isEmpty()) && request?.extras?.containsKey(TelecomManager.EXTRA_INCOMING_CALL_EXTRAS) == true) {
      val incomingExtras = request.extras.getBundle(TelecomManager.EXTRA_INCOMING_CALL_EXTRAS)
      peerId = incomingExtras?.getString(EXTRA_PEER_ID)
    }

    // 4. Fallback
    if (peerId == null || peerId.isEmpty()) {
      peerId = "Unknown"
    }

    val service = CallService.instance ?: run {
      Log.e(TAG, "CallService instance is not available.")
      return Connection.createFailedConnection(android.telecom.DisconnectCause(android.telecom.DisconnectCause.ERROR))
    }

    val connection = MercuryConnection(service, peerId, incoming)

    if (!incoming) {
      connection.setDialing()
    } else {
      connection.setRinging()
    }

    service.registerTelecomConnection(peerId, connection)

    return connection
  }

  companion object {
    private const val TAG = "MercuryConnService"
    const val EXTRA_PEER_ID = "hela.eillance.telecom.PEER_ID"
  }
}
