package me.hela.surveillance.camera

import android.Manifest
import android.app.Activity
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.os.Bundle
import android.os.IBinder
import android.view.Surface
import android.view.TextureView
import android.view.View
import androidx.core.app.ActivityCompat
import kotlinx.android.synthetic.main.activity_main.*


class MainActivity : Activity(), TextureView.SurfaceTextureListener, ServiceConnection {

	private var cameraService: CameraService? = null
	private var cameraServiceBound: Boolean = false

	private var previewSurface: Surface? = null
	private var isPreview: Boolean = false


	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)

		onWindowFocusChanged(true)
		setContentView(R.layout.activity_main)
		setDefaults()
		setEventHandlers()

		checkAndRequestCamera()
	}

	private fun setDefaults() {
		btnTogglePreview.imageAlpha = 75
		btnTogglePreview.isEnabled = false
	}

	private fun setEventHandlers() {
		btnPermissionWarning.setOnClickListener {
			checkAndRequestCamera()
		}

		btnTogglePreview.setOnClickListener {
			isPreview = !isPreview
			cameraService?.startPreview(isPreview)
		}
	}

	override fun onDestroy() {
		super.onDestroy()
		unbindCameraService()
	}

	private fun checkAndRequestCamera() {
		if (ActivityCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
			ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.CAMERA), PERMISSION_REQUEST_CODE_CAMERA)
			return
		}

		permissionGranted()

		startStreamingService()
		createTextureView()
	}

	private fun startStreamingService() {
		val cameraServiceIntent = Intent(applicationContext, CameraService::class.java)
		startService(cameraServiceIntent)
		if (applicationContext.bindService(cameraServiceIntent, this, Context.BIND_AUTO_CREATE + Context.BIND_ABOVE_CLIENT))
			cameraServiceBound = true
	}

	private fun createTextureView() {
		texturePreview.setSurfaceTextureListener(this)
		if (texturePreview.isAvailable()) {
			onSurfaceTextureAvailable(texturePreview.getSurfaceTexture(),
				texturePreview.getWidth(), texturePreview.getHeight())
		}
	}

	private fun resizeTextureView(textureWidth: Int, textureHeight: Int) {
		val cameraPreviewSize = cameraService?.cameraPreviewSize ?: return

		val rotation = windowManager.defaultDisplay.rotation
		var newHeight: Int = textureWidth * cameraPreviewSize.width / cameraPreviewSize.height
		if (Surface.ROTATION_90 == rotation || Surface.ROTATION_270 == rotation) {
			newHeight = textureWidth * cameraPreviewSize.height / cameraPreviewSize.width
		}
		configureTransform(textureWidth, newHeight)
	}

	private fun configureTransform(width: Int, height: Int) {
		val mDisplayOrientation = windowManager.defaultDisplay.rotation * 90
		val matrix = Matrix()
		if (mDisplayOrientation % 180 == 90) {
			//final int width = getWidth();
			//final int height = getHeight();
			// Rotate the camera preview when the screen is landscape.
			matrix.setPolyToPoly(floatArrayOf(
				0f, 0f,  // top left
				width.toFloat(), 0f,  // top right
				0f, height.toFloat(),  // bottom left
				width.toFloat(), height.toFloat()), 0,
				if (mDisplayOrientation == 90) floatArrayOf(
					0f, height.toFloat(),  // top left
					0f, 0f,  // top right
					width.toFloat(), height.toFloat(),  // bottom left
					width.toFloat(), 0f) else floatArrayOf(
					width.toFloat(), 0f,  // top left
					width.toFloat(), height.toFloat(),  // top right
					0f, 0f,  // bottom left
					0f, height.toFloat()), 0,
				4)
		} else if (mDisplayOrientation == 180) {
			matrix.postRotate(180f, width / 2.toFloat(), height / 2.toFloat())
		}
		texturePreview.setTransform(matrix)
	}

	override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
		if (PERMISSION_REQUEST_CODE_CAMERA != requestCode) {
			super.onRequestPermissionsResult(requestCode, permissions, grantResults)
			return
		}

		if (grantResults.size == 1 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
			Thread(Runnable { runOnUiThread {
				checkAndRequestCamera()
			} }).start()
		}
	}

	private fun permissionGranted() {
		btnPermissionWarning.visibility = View.GONE
	}

	private fun unbindCameraService() {
		if (cameraServiceBound) {
			unbindService(this)
			cameraServiceBound = false
		}
	}

	// TextureView.SurfaceTextureListener implementation {
	override fun onSurfaceTextureAvailable(texture: SurfaceTexture?, width: Int, height: Int) {
		btnTogglePreview.imageAlpha = 255
		btnTogglePreview.isEnabled = true

		val cameraPreviewSize = cameraService?.cameraPreviewSize ?: return

		resizeTextureView(width, height)
		texture!!.setDefaultBufferSize(cameraPreviewSize.width, cameraPreviewSize.height)

		val surface = Surface(texture)
		previewSurface = surface

		cameraService?.previewSurfaceCreated(surface)
	}

	override fun onSurfaceTextureDestroyed(surface: SurfaceTexture?): Boolean {
		btnTogglePreview.imageAlpha = 75
		btnTogglePreview.isEnabled = false

		cameraService?.previewSurfaceDestroyed(previewSurface!!)

		previewSurface = null
		return true
	}

	override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture?, width: Int, height: Int) { }

	override fun onSurfaceTextureUpdated(surface: SurfaceTexture?) { }
	// TextureView.SurfaceTextureListener implementation }

	// ServiceConnection implementation {
	override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
		cameraService = (service as CameraService.StreamingServiceBinder).service
		createTextureView()
	}

	override fun onServiceDisconnected(name: ComponentName?) {
		cameraService = null
	}
	// ServiceConnection implementation }


	companion object {
		private const val PERMISSION_REQUEST_CODE_CAMERA: Int = 1
	}
}
