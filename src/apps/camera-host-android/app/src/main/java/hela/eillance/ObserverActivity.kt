package hela.eillance

import android.os.Bundle
import android.view.Surface
import android.view.SurfaceHolder
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import hela.eillance.camera.databinding.ActivityObserverBinding
import androidx.core.view.WindowInsetsCompat
import hela.eillance.ndkio.interfaces.IObserverNativeBridge

class ObserverActivity : AppCompatActivity(),
  SurfaceHolder.Callback,
  IObserverNativeBridge
{
  private lateinit var binding: ActivityObserverBinding

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

    binding.outputSurface.holder.addCallback(this)
  }

  override fun surfaceCreated(holder: SurfaceHolder) {
    initDecoder(this, holder.surface)
  }

  override fun surfaceDestroyed(holder: SurfaceHolder) {
    releaseDecoder()
  }

  override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
  }

  private external fun initDecoder(nativeBridge: IObserverNativeBridge, surface: Surface)
  private external fun releaseDecoder()

  companion object {
    init {
      System.loadLibrary("lib-observer-service-bridge")
    }
  }

  override fun onStreamResolutionChange(width: Int, height: Int) {
    val newHeight = binding.outputSurface.width * height / width
    // TODO: make it work properly
    binding.outputSurface.layoutParams.height = newHeight
    binding.outputSurface.requestLayout()
  }
}
