package hela.eillance

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import hela.eillance.camera.R
import hela.eillance.ndkio.Peer
import hela.eillance.ndkio.Profile
import hela.eillance.ndkio.VideoConfig
import hela.eillance.ndkio.interfaces.IClientNativeBridge
import java.io.Serializable

class CallService : Service(), IClientNativeBridge {

  inner class CallServiceBinder : Binder() {
    fun getService(): CallService = this@CallService
  }

  private val binder = CallServiceBinder()

  override fun onBind(intent: Intent?): IBinder = binder

  override fun onCreate() {
    super.onCreate()
    initEngine(this)
    start()
    startForegroundService()
  }

  override fun onDestroy() {
    terminate()
    super.onDestroy()
  }

  private fun startForegroundService() {
    val channelId = createNotificationChannel()
    val notificationIntent = Intent(this, MainActivity::class.java)
    val pendingIntent = PendingIntent.getActivity(this, 0, notificationIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val notification: Notification = NotificationCompat.Builder(this, channelId)
      .setContentTitle(getString(R.string.app_name))
      .setContentText("Running in background")
      .setSmallIcon(R.drawable.ic_camera_stream)
      .setContentIntent(pendingIntent)
      .build()

    try {
      startForeground(SERVICE_ID, notification)
    } catch (e: Exception) {
      Log.e("CallService", "Failed to start foreground service", e)
    }
  }

  private fun createNotificationChannel(): String {
    val channelId = "call_service_channel"
    val channelName = "Call Service"
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      val chan = NotificationChannel(channelId, channelName, NotificationManager.IMPORTANCE_NONE)
      chan.lockscreenVisibility = Notification.VISIBILITY_PRIVATE
      val service = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
      service.createNotificationChannel(chan)
    }
    return channelId
  }

  override fun onStatusUpdate(peerId: String, source: Int, status: Int) {
    val intent = Intent(Tune.BroadcastActionOnStateUpdate).apply {
      putExtra(Tune.BroadcastActionOnStateUpdate_peer_id, peerId)
      putExtra(Tune.BroadcastActionOnStateUpdate_source, source)
      putExtra(Tune.BroadcastActionOnStateUpdate_state, status)
    }
    LocalBroadcastManager.getInstance(this).sendBroadcast(intent)
  }

  override fun onStreamInfoChange(width: Int, height: Int, rotation: Int) {
    val intent = Intent(ACTION_STREAM_INFO).apply {
      putExtra(EXTRA_STREAM_WIDTH, width)
      putExtra(EXTRA_STREAM_HEIGHT, height)
      putExtra(EXTRA_STREAM_ROTATION, rotation)
    }
    LocalBroadcastManager.getInstance(this).sendBroadcast(intent)
  }

  override fun onDeviceProfileReceived(peerId: String, profile: Profile) {
    // This can be used to update a specific peer's details if needed
  }

  override fun onPeerListUpdate(peers: List<Peer>) {
    val intent = Intent(ACTION_PEER_LIST_UPDATE).apply {
      // Peer is not serializable by default, so we wrap it or extract primitives
      val serializablePeers = ArrayList<HashMap<String, Serializable>>()
      peers.forEach { peer ->
        val map = hashMapOf<String, Serializable>()
        map["id"] = peer.id
        map["name"] = peer.name
        // Skipping profile for simplicity in broadcast
        serializablePeers.add(map)
      }
      putExtra(EXTRA_PEERS, serializablePeers)
    }
    LocalBroadcastManager.getInstance(this).sendBroadcast(intent)
  }


  private external fun initEngine(nativeBridge: IClientNativeBridge)
  private external fun start()
  private external fun terminate()
  external fun initDecoder(surface: android.view.Surface)
  external fun releaseDecoder()
  external fun requestPeerListUpdate()
  external fun call(peerId: String)
  external fun addContact(peerId: String)
  external fun configureVideoCapture(config: VideoConfig)

  companion object {
    private const val SERVICE_ID = 101
    const val ACTION_PEER_LIST_UPDATE = "action_peer_list_update"
    const val EXTRA_PEERS = "extra_peers"

    const val ACTION_STREAM_INFO = "action_stream_info"
    const val EXTRA_STREAM_WIDTH = "extra_stream_width"
    const val EXTRA_STREAM_HEIGHT = "extra_stream_height"
    const val EXTRA_STREAM_ROTATION = "extra_stream_rotation"

    init {
      System.loadLibrary("lib-client-service-bridge")
    }
  }
}
