package hela.eillance.webrtc

import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.io.EOFException
import java.net.SocketException
import java.net.SocketTimeoutException
import java.net.UnknownHostException
import java.net.UnknownServiceException
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

sealed class SignalingMessage {
  data class PeerMessage(val message: JSONObject) : SignalingMessage()
  data class Error(val error: String, val forPeer: String?) : SignalingMessage()
  data class SystemMessage(val type: String, val payload: JSONObject) : SignalingMessage()
}

class SignalingClient {
  enum class State {
    CONNECTING,
    OPEN,
    CLOSED,
    FAILED,
    NO_NETWORK,
    INVALID_TOKEN
  }

  private val clientScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
  private var webSocket: WebSocket? = null
  private val isConnected = AtomicBoolean(false)
  private val isAuthenticated = AtomicBoolean(false)

  private val _messages = MutableSharedFlow<SignalingMessage>()
  val messages: SharedFlow<SignalingMessage> = _messages

  private val _state = MutableSharedFlow<State>(replay = 1)
  val state: SharedFlow<State> = _state

  private val messageQueue = ConcurrentLinkedQueue<String>()

  fun connect(peerId: String, signalingUrl: String) {
    Log.i(TAG, "Connect called. Starting connection manager coroutine.")
    clientScope.launch {
      var currentBackoffMs = INITIAL_BACKOFF_MS
      _state.tryEmit(State.CONNECTING)
      val hasFatalAuthError = AtomicBoolean(false)
      while (isActive && !hasFatalAuthError.get()) {
        val sessionClosedSignal = CompletableDeferred<Unit>()

        if (!isConnected.get()) {
          Log.i(TAG, "Attempting to connect to signaling server...")
          _state.tryEmit(State.CONNECTING)

          val listener = object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
              Log.i(TAG, "Signaling connection opened.")
              this@SignalingClient.webSocket = webSocket
              isConnected.set(true)
              isAuthenticated.set(false)
              currentBackoffMs = INITIAL_BACKOFF_MS
              _state.tryEmit(State.OPEN)

              // Send login
              val loginMessage = JSONObject().apply {
                put("type", "login")
                put("id", peerId)
              }
              webSocket.send(loginMessage.toString())
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
              Log.d(TAG, "Received: $text")
              try {
                val json = JSONObject(text)
                
                if (json.has("type") && json.getString("type") == "login_success") {
                  isAuthenticated.set(true)
                  while (!messageQueue.isEmpty()) {
                    val msg = messageQueue.poll()
                    if (msg != null) {
                      webSocket.send(msg)
                    }
                  }
                } else if (json.has("type") && json.getString("type") == "error") {
                  val error = json.getString("error")
                  if (error == "auth_failed" || error == "invalid_id") {
                    hasFatalAuthError.set(true)
                    _state.tryEmit(State.INVALID_TOKEN)
                  }
                }

                val signalingMessage = when {
                  json.has("from") -> SignalingMessage.PeerMessage(json)
                  json.has("type") && json.getString("type") == "error" -> {
                    val forPeer = if (json.has("for_peer")) json.getString("for_peer") else null
                    SignalingMessage.Error(json.getString("error"), forPeer)
                  }

                  else -> SignalingMessage.SystemMessage(json.getString("type"), json)
                }
                clientScope.launch { _messages.emit(signalingMessage) }
              } catch (e: Exception) {
                Log.e(TAG, "Failed to parse message: $text", e)
              }
            }

            override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
              Log.w(TAG, "Signaling connection closing: $code - $reason")
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
              Log.i(TAG, "Signaling connection closed.")
              isConnected.set(false)
              isAuthenticated.set(false)
              this@SignalingClient.webSocket = null
              if (!hasFatalAuthError.get()) {
                _state.tryEmit(State.CLOSED)
              }
              sessionClosedSignal.complete(Unit)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
              val state = determineErrorState(t)
              Log.e(TAG, "Signaling failure: $state. Reason: ${t.message}")
              isConnected.set(false)
              isAuthenticated.set(false)
              this@SignalingClient.webSocket = null
              if (!hasFatalAuthError.get()) {
                _state.tryEmit(state)
              }
              sessionClosedSignal.complete(Unit)
            }
          }

          val client = OkHttpClient.Builder()
            .connectTimeout(10, TimeUnit.SECONDS)
            .readTimeout(0, TimeUnit.SECONDS)
            .pingInterval(20, TimeUnit.SECONDS)
            .build()

          val urlToConnect = if (signalingUrl.isBlank()) DEFAULT_SIGNALING_URL else signalingUrl
          Log.i(TAG, "Connecting to: $urlToConnect")
          val request = Request.Builder().url(urlToConnect).build()
          client.newWebSocket(request, listener)
        }

        sessionClosedSignal.await()

        if (isActive && !hasFatalAuthError.get()) {
          Log.i(TAG, "Will attempt to reconnect in ${currentBackoffMs / 1000} seconds.")
          delay(currentBackoffMs)
          currentBackoffMs = (currentBackoffMs * 2).coerceAtMost(MAX_BACKOFF_MS)
        }
      }
    }
  }

  fun send(message: JSONObject) {
    val messageString = message.toString()
    val ws = webSocket
    
    // System messages required for handshake must bypass the queue
    val isAuthMessage = message.optString("type") == "auth_response"

    if (isConnected.get() && ws != null && (isAuthenticated.get() || isAuthMessage)) {
      if (!ws.send(messageString)) {
        Log.e(TAG, "Failed to send message immediately, queuing.")
        if (!isAuthMessage) {
          messageQueue.add(messageString)
        }
      }
    } else {
      if (!isAuthMessage) {
        Log.i(TAG, "Signaling not ready, queuing message (type: ${message.optString("type")}).")
        messageQueue.add(messageString)
      }
    }
  }

  fun disconnect() {
    Log.i(TAG, "Disconnect called.")
    clientScope.cancel()
    webSocket?.close(NORMAL_CLOSURE_STATUS, "Client disconnected")
    webSocket = null
    isConnected.set(false)
    isAuthenticated.set(false)
    _state.tryEmit(State.CLOSED)
    messageQueue.clear()
  }

  private fun determineErrorState(t: Throwable): State {
    return when (t) {
      is UnknownHostException -> State.NO_NETWORK
      is SocketException -> {
        if (t.message?.contains("Network is unreachable", ignoreCase = true) == true) {
          State.NO_NETWORK
        } else {
          State.FAILED
        }
      }

      is SocketTimeoutException, is UnknownServiceException, is EOFException -> State.FAILED
      else -> State.FAILED
    }
  }

  companion object {
    private const val TAG = "SignalingClient"
    const val DEFAULT_SIGNALING_URL = "wss://app.hela.me:8000"
    private const val NORMAL_CLOSURE_STATUS = 1000

    private const val INITIAL_BACKOFF_MS = 2000L
    private const val MAX_BACKOFF_MS = 30000L
  }
}
