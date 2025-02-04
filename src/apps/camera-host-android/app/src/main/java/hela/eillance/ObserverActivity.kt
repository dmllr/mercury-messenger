package hela.eillance

import android.graphics.SurfaceTexture
import android.os.Bundle
import android.view.Surface
import android.view.TextureView.SurfaceTextureListener
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import hela.eillance.camera.databinding.ActivityObserverBinding
import androidx.core.view.WindowInsetsCompat

class ObserverActivity : AppCompatActivity(), SurfaceTextureListener {
  private lateinit var binding: ActivityObserverBinding
  private lateinit var surface: Surface

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    enableEdgeToEdge()
    binding = ActivityObserverBinding.inflate(layoutInflater)
    setContentView(binding.root)
    ViewCompat.setOnApplyWindowInsetsListener(binding.main) { v, insets ->
      val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
      v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
      insets
    }

    binding.outputSurface.apply {
      surfaceTextureListener = this@ObserverActivity
    }
  }

  override fun onSurfaceTextureAvailable(stx: SurfaceTexture, width: Int, height: Int) {
    surface = Surface(stx)
    initDecoder(surface)
  }

  override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
    releaseDecoder()
    surface.release()
    return true
  }

  override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
  }

  override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
  }

  private external fun initDecoder(surface: Surface)
  private external fun releaseDecoder()

  companion object {
    init {
      System.loadLibrary("lib-observer-service-bridge")
    }
  }
}
