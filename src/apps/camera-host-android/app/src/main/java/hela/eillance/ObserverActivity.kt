package hela.eillance

import android.os.Bundle
import android.util.Log
import android.view.Surface
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import hela.eillance.camera.R
import hela.eillance.camera.databinding.ActivityObserverBinding
import hela.eillance.fragments.ObserverDevicesFragment
import hela.eillance.ndkio.ClientInfo
import hela.eillance.ndkio.interfaces.IObserverNativeBridge

class ObserverActivity : AppCompatActivity(), IObserverNativeBridge {
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

    initEngine(this)

    supportFragmentManager.beginTransaction()
      .replace(R.id.content, ObserverDevicesFragment.instance())
      .commit()
  }

  private external fun initEngine(nativeBridge: IObserverNativeBridge)
  external fun initDecoder(surface: Surface)
  external fun releaseDecoder()

  companion object {
    init {
      System.loadLibrary("lib-observer-service-bridge")
    }
  }

  override fun onStreamResolutionChange(width: Int, height: Int) {
    if (width == 0 || height == 0) {
      return
    }

//    binding.outputSurface.post {
//      val surfaceWidth = binding.outputSurface.width
//      val newHeight = surfaceWidth * height / width
//
//      binding.outputSurface.layoutParams = binding.outputSurface.layoutParams.apply {
//        this.height = newHeight
//      }
//
//      binding.outputSurface.requestLayout() // Ensure UI updates
//    }
  }

  override fun onClientListUpdate(clients: List<ClientInfo>) {
//    TODO("Not yet implemented")
    Log.i("AA", "Client list updated: $clients")
  }
}
