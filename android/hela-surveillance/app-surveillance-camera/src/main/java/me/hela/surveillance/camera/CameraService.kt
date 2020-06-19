package me.hela.surveillance.camera

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import android.util.Size
import android.view.Surface
import androidx.annotation.RequiresApi
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationCompat.PRIORITY_MIN
import me.helpers.doAsync

class CameraService : Service() {
	private var cameraEngine: Long = 0
	var cameraPreviewSize: Size = Size(0, 0)


	inner class StreamingServiceBinder : Binder() {
		val service: CameraService
			get() = this@CameraService
	}
	private val streamingServiceBinder = StreamingServiceBinder()

	override fun onCreate() {
		super.onCreate()

		if (isCamera2Device()) {
			createNativeCamera()
		} else {
			Log.e("CameraSample", "Found legacy camera device, camera2 device required")
			return
		}

		startForeground()
	}

	override fun onDestroy() {
		deleteCameraEngine(cameraEngine)

		super.onDestroy()
	}

	override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
		if (intent != null)
			startForeground()

		return START_STICKY;
	}

	override fun onBind(intent: Intent): IBinder? {
		return streamingServiceBinder
	}

	private fun isCamera2Device(): Boolean {
		val camMgr = getSystemService(Context.CAMERA_SERVICE) as CameraManager
		var camera2Dev = true
		try {
			val cameraIds = camMgr.cameraIdList
			if (cameraIds.isNotEmpty()) {
				for (id in cameraIds) {
					val characteristics = camMgr.getCameraCharacteristics(id)
					val deviceLevel = characteristics.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)!!
					val facing = characteristics.get(CameraCharacteristics.LENS_FACING)!!
					if (deviceLevel == CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY && facing == CameraMetadata.LENS_FACING_BACK)
						camera2Dev = false
				}
			}
		} catch (e: CameraAccessException) {
			e.printStackTrace()
			camera2Dev = false
		}

		return camera2Dev
	}

	private fun createNativeCamera() {
//		val display = windowManager.defaultDisplay
//		val height = display.mode.physicalHeight
//		val width = display.mode.physicalWidth
		val height = 320
		val width = 240
		cameraEngine = createCameraEngine(width, height)
		cameraPreviewSize = getMinimumCompatiblePreviewSize(cameraEngine)
	}

	private fun startForeground() {
		val channel =
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
				createNotificationChannel()
			else ""

		val notificationBuilder = NotificationCompat.Builder(this, channel)
		val builder = notificationBuilder.setOngoing(true)
			.setSmallIcon(R.drawable.ic_camera_stream)
			.setPriority(PRIORITY_MIN)
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
			builder.setCategory(Notification.CATEGORY_SERVICE)

		val notification = builder.build()
		notification.flags + Notification.FLAG_NO_CLEAR

		startForeground(CAMERA_SERVICE_FG_ID, notification)
	}

	@RequiresApi(Build.VERSION_CODES.O)
	private fun createNotificationChannel(): String {
		val id = "surveillance-foreground-service"
		val chan = NotificationChannel(
			id,
			getString(R.string.notification_description),
			NotificationManager.IMPORTANCE_LOW)
		chan.lockscreenVisibility = Notification.VISIBILITY_PRIVATE

		val service = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

		service.createNotificationChannel(chan)

		return id
	}

	fun startPreview(preview: Boolean) {
		requestPreview(cameraEngine, preview)
	}

	fun startRtc() {
		requestRtc(cameraEngine)
	}

	fun previewSurfaceCreated(surface: Surface) {
		onPreviewSurfaceCreated(cameraEngine, surface)
	}

	fun previewSurfaceDestroyed(surface: Surface) {
		onPreviewSurfaceDestroyed(cameraEngine, surface)
	}


	// camera-lib
	private external fun createCameraEngine(width: Int, height: Int): Long
	private external fun deleteCameraEngine(ndkCamera: Long)
	private external fun getMinimumCompatiblePreviewSize(ndkCamera: Long): Size

	private external fun requestPreview(ndkCamera: Long, preview: Boolean)
	private external fun requestRtc(ndkCamera: Long)

	private external fun onPreviewSurfaceCreated(ndkCamera: Long, surface: Surface)
	private external fun onPreviewSurfaceDestroyed(ndkCamera: Long, surface: Surface)


	companion object {
		private const val CAMERA_SERVICE_FG_ID = 9990

		init {
			System.loadLibrary("camera-lib")
		}
	}
}
