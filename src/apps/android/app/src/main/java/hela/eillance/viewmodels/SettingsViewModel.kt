package hela.eillance.viewmodels

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.events.NativeEventBus
import hela.eillance.ndkio.ServiceConnectionState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class NetworkStats(
  val connectedPeers: Int = 0,
  val bootstrapNodesAlive: Int = 0,
  val dhtPeers: Int = 0,
  val deviceRole: String = "simple_node",
  val lastUpdate: Long = 0L
)

class SettingsViewModel(application: Application) : AndroidViewModel(application) {

  private var nativePtr: Long = 0
  private val _connectionState = MutableStateFlow(ServiceConnectionState.Unknown)
  val connectionState = _connectionState.asStateFlow()

  private val _bootstrapNodeUrls = MutableStateFlow("")
  val bootstrapNodeUrls = _bootstrapNodeUrls.asStateFlow()

  private val _appendBootstrapNodes = MutableStateFlow(true)
  val appendBootstrapNodes = _appendBootstrapNodes.asStateFlow()

  private val _peerInactivityTimeout = MutableStateFlow(120)
  val peerInactivityTimeout = _peerInactivityTimeout.asStateFlow()

  private val _customTurn = MutableStateFlow("")
  val customTurn = _customTurn.asStateFlow()

  private val _useCustomTurnOnly = MutableStateFlow(false)
  val useCustomTurnOnly = _useCustomTurnOnly.asStateFlow()

  private val _playSoundOnSend = MutableStateFlow(true)
  val playSoundOnSend = _playSoundOnSend.asStateFlow()

  private val _playSoundOnReceive = MutableStateFlow(true)
  val playSoundOnReceive = _playSoundOnReceive.asStateFlow()

  private val _showNotificationContent = MutableStateFlow(true)
  val showNotificationContent = _showNotificationContent.asStateFlow()

  private val _networkStats = MutableStateFlow(NetworkStats())
  val networkStats = _networkStats.asStateFlow()

  init {
    nativePtr = nativeCreate()
    if (nativePtr == 0L) {
      Log.e("SettingsViewModel", "Failed to create native SettingsViewModel.")
    }
    viewModelScope.launch {
      NativeEventBus.settings.collect { s ->
        if (s != null) {
          _bootstrapNodeUrls.value = s.bootstrapNodeUrls ?: ""
          _appendBootstrapNodes.value = s.appendBootstrapNodes
          _peerInactivityTimeout.value = s.timeout
          _customTurn.value = s.customTurn ?: ""
          _useCustomTurnOnly.value = s.useCustomTurnOnly
          _playSoundOnSend.value = s.playSoundOnSend
          _playSoundOnReceive.value = s.playSoundOnReceive
          _showNotificationContent.value = s.showNotificationContent
        }
      }
    }
    viewModelScope.launch {
      NativeEventBus.networkStats.collect { stats ->
        _networkStats.value = stats
      }
    }
    viewModelScope.launch {
      NativeEventBus.connectionState.collect { _connectionState.value = it }
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  fun onBootstrapNodeUrlsChange(newUrls: String) {
    _bootstrapNodeUrls.value = newUrls
  }

  fun onAppendBootstrapNodesChange(append: Boolean) {
    _appendBootstrapNodes.value = append
  }

  fun onPeerInactivityTimeoutChange(newValue: Int) {
    _peerInactivityTimeout.value = newValue
  }

  fun onCustomTurnChange(newValue: String) {
    _customTurn.value = newValue
  }

  fun onUseCustomTurnOnlyChange(onlyCustom: Boolean) {
    _useCustomTurnOnly.value = onlyCustom
  }

  fun onShowNotificationContentChange(enabled: Boolean) {
    if (nativePtr != 0L) nativeSetShowNotificationContent(nativePtr, enabled)
  }

  fun onPlaySoundOnSendChange(enabled: Boolean) {
    if (nativePtr != 0L) nativeSetSoundSettings(nativePtr, enabled, _playSoundOnReceive.value)
  }

  fun onPlaySoundOnReceiveChange(enabled: Boolean) {
    if (nativePtr != 0L) nativeSetSoundSettings(nativePtr, _playSoundOnSend.value, enabled)
  }

  sealed class SaveResult {
    data object Success : SaveResult()
    data class InvalidBootstrap(val invalidEntries: List<String>) : SaveResult()
    data class InvalidTurn(val invalidEntries: List<String>) : SaveResult()
  }

  fun validateBootstrapUrls(input: String): List<String> {
    val invalid = mutableListOf<String>()
    val delimiters = charArrayOf('\n', '\r', ',', ';')
    val tokens = input.split(*delimiters).map { it.trim() }.filter { it.isNotEmpty() }
    for (token in tokens) {
      if (!isValidBootstrapUrl(token)) {
        invalid.add(token)
      }
    }
    return invalid
  }

  private fun isValidBootstrapUrl(url: String): Boolean {
    if (!url.startsWith("/")) return false
    val parts = url.split("/").filter { it.isNotEmpty() }
    if (parts.size != 4 && parts.size != 6) return false

    val proto = parts[0]
    if (proto !in listOf("dns", "dns4", "dns6", "ip4", "ip6")) return false

    val host = parts[1]
    if (!isValidHost(proto, host)) return false

    val trans = parts[2]
    if (trans !in listOf("tcp", "udp", "quic", "quic-v1")) return false

    val port = parts[3].toIntOrNull() ?: return false
    if (port <= 0 || port > 65535) return false

    return true
  }

  private fun isValidHost(proto: String, host: String): Boolean {
    return when (proto) {
      "ip4" -> isValidIpv4(host)
      "ip6" -> isValidIpv6(host)
      "dns", "dns4", "dns6" -> isValidDomainName(host)
      else -> false
    }
  }

  private fun isValidIpv4(ip: String): Boolean {
    val segments = ip.split(".")
    if (segments.size != 4) return false
    return segments.all { seg ->
      val num = seg.toIntOrNull() ?: return false
      num in 0..255 && (seg == "0" || !seg.startsWith("0"))
    }
  }

  private fun isValidIpv6(ip: String): Boolean {
    return try {
      java.net.Inet6Address.getByName(ip) is java.net.Inet6Address
    } catch (_: Exception) {
      false
    }
  }

  private fun isValidDomainName(domain: String): Boolean {
    if (domain.isEmpty() || domain.length > 253) return false
    if (isValidIpv4(domain)) return true
    val labels = domain.split(".")
    return labels.all { label ->
      label.isNotEmpty() && label.length <= 63 &&
          !label.startsWith("-") && !label.endsWith("-") &&
          label.all { it.isLetterOrDigit() || it == '-' }
    }
  }

  fun validateTurnUrls(input: String): List<String> {
    val invalid = mutableListOf<String>()
    val delimiters = charArrayOf('\n', '\r', ',', ';')
    val tokens = input.split(*delimiters).map { it.trim() }.filter { it.isNotEmpty() }
    for (token in tokens) {
      if (!isValidTurnUrl(token)) {
        invalid.add(token)
      }
    }
    return invalid
  }

  private fun isValidTurnUrl(raw: String): Boolean {
    var s = raw.trim()
    if (s.isEmpty()) return true

    if (s.startsWith("turn://") || s.startsWith("stun://")) {
      s = s.substring(7)
    } else if (s.startsWith("turns://") || s.startsWith("stuns://")) {
      s = s.substring(8)
    } else if (s.startsWith("turn:") || s.startsWith("stun:")) {
      s = s.substring(5)
    } else if (s.startsWith("turns:") || s.startsWith("stuns:")) {
      s = s.substring(6)
    } else if (s.contains("://")) {
      return false
    }

    if (s.contains('@')) {
      val atPos = s.indexOf('@')
      val userPass = s.substring(0, atPos)
      s = s.substring(atPos + 1)
      val username = if (userPass.contains(':')) userPass.substringBefore(':') else userPass
      if (username.isEmpty() || username.contains(' ')) return false
    }

    val hostPort = if (s.contains('?')) s.substringBefore('?') else s
    val query = if (s.contains('?')) s.substringAfter('?') else ""

    if (query.isNotEmpty() && query != "transport=udp" && query != "transport=tcp") {
      return false
    }

    if (hostPort.isEmpty() || hostPort.contains(' ') || hostPort.contains('/')) return false

    val host: String
    val portStr: String

    if (hostPort.startsWith('[')) {
      val closeBracket = hostPort.indexOf(']')
      if (closeBracket == -1 || closeBracket + 2 >= hostPort.length || hostPort[closeBracket + 1] != ':') {
        return false
      }
      host = hostPort.substring(1, closeBracket)
      portStr = hostPort.substring(closeBracket + 2)
      if (!isValidIpv6(host)) return false
    } else {
      val firstColon = hostPort.indexOf(':')
      val lastColon = hostPort.lastIndexOf(':')
      if (firstColon == -1 || firstColon != lastColon) {
        return false
      }
      host = hostPort.substring(0, firstColon)
      portStr = hostPort.substring(firstColon + 1)
      if (!isValidDomainName(host)) return false
    }

    val port = portStr.toIntOrNull() ?: return false
    return port in 1..65535
  }

  fun saveSettings(): SaveResult {
    val invalidBootstrap = validateBootstrapUrls(_bootstrapNodeUrls.value)
    if (invalidBootstrap.isNotEmpty()) {
      return SaveResult.InvalidBootstrap(invalidBootstrap)
    }
    val invalidTurn = validateTurnUrls(_customTurn.value)
    if (invalidTurn.isNotEmpty()) {
      return SaveResult.InvalidTurn(invalidTurn)
    }
    if (nativePtr != 0L) {
      nativeUpdateBootstrapNodeUrls(nativePtr, _bootstrapNodeUrls.value, _appendBootstrapNodes.value)
      nativeSetTimeout(nativePtr, _peerInactivityTimeout.value)
      nativeUpdateIceConfig(
        nativePtr,
        _customTurn.value,
        _useCustomTurnOnly.value
      )
    }
    return SaveResult.Success
  }

  private external fun nativeCreate(): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeUpdateBootstrapNodeUrls(ptr: Long, urls: String, appendToDefault: Boolean)
  private external fun nativeSetTimeout(ptr: Long, seconds: Int)
  private external fun nativeUpdateIceConfig(
    ptr: Long,
    turn: String,
    useCustomTurnOnly: Boolean
  )

  private external fun nativeSetSoundSettings(ptr: Long, send: Boolean, receive: Boolean)
  private external fun nativeSetShowNotificationContent(ptr: Long, show: Boolean)
}
