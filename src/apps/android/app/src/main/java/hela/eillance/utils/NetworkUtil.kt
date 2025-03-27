package hela.eillance.utils

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Utility class to monitor network state and check connectivity
 */
class NetworkUtil(context: Context) {

  private val connectivityManager: ConnectivityManager =
    context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

  private val _networkAvailable = MutableLiveData<Boolean>()
  val networkAvailable: LiveData<Boolean> = _networkAvailable

  private val _networkQuality = MutableLiveData<NetworkQuality>()

  private val coroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

  private val networkCallback = object : ConnectivityManager.NetworkCallback() {
    override fun onAvailable(network: Network) {
      _networkAvailable.postValue(true)
      startQualityMonitoring()
    }

    override fun onLost(network: Network) {
      _networkAvailable.postValue(false)
      _networkQuality.postValue(NetworkQuality.UNAVAILABLE)
    }

    override fun onCapabilitiesChanged(
      network: Network,
      networkCapabilities: NetworkCapabilities
    ) {
      // Update quality based on capabilities
      val quality = when {
        networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> NetworkQuality.EXCELLENT
        networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> {
          if (networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)) {
            NetworkQuality.GOOD
          } else {
            NetworkQuality.MODERATE
          }
        }
        else -> NetworkQuality.POOR
      }
      _networkQuality.postValue(quality)
    }
  }

  init {
    checkNetworkAndInitialize()
  }

  enum class NetworkQuality {
    EXCELLENT, GOOD, MODERATE, POOR, UNAVAILABLE
  }

  /**
   * Start monitoring network state changes
   */
  fun startMonitoring() {
    val request = NetworkRequest.Builder()
      .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
      .build()
    try {
      connectivityManager.registerNetworkCallback(request, networkCallback)
    } catch (e: Exception) {
      // Already registered or other error
    }
    startQualityMonitoring()
  }

  /**
   * Stop monitoring network state
   */
  fun stopMonitoring() {
    try {
      connectivityManager.unregisterNetworkCallback(networkCallback)
    } catch (e: Exception) {
      // Ignore if already unregistered
    }
  }

  /**
   * Check if network is currently available
   */
  fun isNetworkAvailable(): Boolean {
    val networkCapabilities = connectivityManager.activeNetwork ?: return false
    val actNw = connectivityManager.getNetworkCapabilities(networkCapabilities) ?: return false
    return actNw.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
  }

  /**
   * Check internet connectivity by attempting a lightweight connection
   */
  suspend fun hasActualInternetConnection(): Boolean = withContext(Dispatchers.IO) {
    try {
      val runtime = Runtime.getRuntime()
      val process = runtime.exec("/system/bin/ping -c 1 8.8.8.8")
      val exitValue = process.waitFor()
      exitValue == 0
    } catch (_: Exception) {
      false
    }
  }

  private fun startQualityMonitoring() {
    coroutineScope.launch {
      while (isNetworkAvailable()) {
        if (!hasActualInternetConnection()) {
          _networkQuality.postValue(NetworkQuality.POOR)
        }
        delay(30000) // Check every 30 seconds
      }
    }
  }

  private fun checkNetworkAndInitialize() {
    _networkAvailable.value = isNetworkAvailable()
    if (isNetworkAvailable()) {
      startQualityMonitoring()
    } else {
      _networkQuality.value = NetworkQuality.UNAVAILABLE
    }
  }
}
