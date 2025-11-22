package hela.eillance.telecom

import android.net.Uri
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
    Log.d(TAG, "onCreateIncomingConnection")
    return createConnection(request, incoming = true)
  }

  override fun onCreateOutgoingConnection(
    connectionManagerPhoneAccount: PhoneAccountHandle?,
    request: ConnectionRequest?
  ): Connection {
    Log.d(TAG, "onCreateOutgoingConnection")
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
    val peerId = request?.extras?.getString(EXTRA_PEER_ID) ?: "Unknown"

    // Establish the link with the CallService singleton or bound instance
    val service = CallService.instance ?: run {
      Log.e(TAG, "CallService instance is not available.")
      return Connection.createFailedConnection(android.telecom.DisconnectCause(android.telecom.DisconnectCause.ERROR))
    }

    val connection = MercuryConnection(service, peerId, incoming)

    // If this is an outgoing call, we need to tell the CallService that the telecom connection is ready
    // so it can proceed with the engine logic if waiting.
    if (!incoming) {
      connection.setDialing()
    } else {
      connection.setRinging()
    }

    // Register this connection with the CallService so state changes can be propagated
    service.registerTelecomConnection(peerId, connection)

    return connection
  }

  companion object {
    private const val TAG = "MercuryConnService"
    const val EXTRA_PEER_ID = "hela.eillance.telecom.PEER_ID"
  }
}
