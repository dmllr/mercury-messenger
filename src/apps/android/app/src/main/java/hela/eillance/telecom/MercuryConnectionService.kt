package hela.eillance.telecom

import android.telecom.Connection
import android.telecom.ConnectionRequest
import android.telecom.ConnectionService
import android.telecom.PhoneAccountHandle
import android.telecom.TelecomManager
import android.util.Log
import hela.eillance.camera.R
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
    // 1. Try to get ID from the extras (passed from CallService)
    var peerId = request?.extras?.getString(EXTRA_PEER_ID)

    // 2. Try nested extras (Android Telecom sometimes wraps the Bundle)
    if (peerId.isNullOrEmpty() && request?.extras?.containsKey(TelecomManager.EXTRA_INCOMING_CALL_EXTRAS) == true) {
      val incomingExtras = request.extras.getBundle(TelecomManager.EXTRA_INCOMING_CALL_EXTRAS)
      peerId = incomingExtras?.getString(EXTRA_PEER_ID)
    }

    // 3. Fallback to address parsing
    if (peerId.isNullOrEmpty()) {
      peerId = request?.address?.schemeSpecificPart
    }

    if (peerId.isNullOrEmpty()) {
      Log.e(TAG, "Failed to determine peerId for connection")
      return Connection.createFailedConnection(
        android.telecom.DisconnectCause(android.telecom.DisconnectCause.ERROR, getString(R.string.telecom_error_unknown_peer))
      )
    }

    val service = CallService.instance
    if (service == null) {
      Log.e(TAG, "CallService instance is not available. Cannot create connection.")
      return Connection.createFailedConnection(
        android.telecom.DisconnectCause(android.telecom.DisconnectCause.ERROR, getString(R.string.telecom_error_service_disconnected))
      )
    }

    val connection = MercuryConnection(service, peerId, incoming)

    // Set initial state based on direction
    if (incoming) {
      connection.setRinging()
    } else {
      connection.setDialing()
    }

    // Register this connection with the CallService to synchronize state with the C++ engine
    service.registerTelecomConnection(peerId, connection)

    Log.i(TAG, "Created new ${if(incoming) "incoming" else "outgoing"} connection for $peerId")
    return connection
  }

  companion object {
    private const val TAG = "MercuryConnService"
    const val EXTRA_PEER_ID = "hela.eillance.telecom.PEER_ID"
  }
}
