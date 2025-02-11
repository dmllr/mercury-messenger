package hela.eillance.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.Surface
import android.view.SurfaceHolder
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import hela.eillance.camera.databinding.FragmentObserverOperationBinding
import hela.eillance.ndkio.interfaces.IObserverNativeBridge

class ObserverOperationFragment : Fragment(), SurfaceHolder.Callback, IObserverNativeBridge {

  private var _binding: FragmentObserverOperationBinding? = null
  private val binding get() = _binding!!

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    _binding = FragmentObserverOperationBinding.inflate(inflater, container, false)
    binding.outputSurface.holder.addCallback(this)

    return binding.root
  }

  override fun onDestroyView() {
    super.onDestroyView()
    _binding = null
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
    fun instance() = ObserverOperationFragment()
    init {
      System.loadLibrary("lib-observer-service-bridge")
    }
  }

  override fun onStreamResolutionChange(width: Int, height: Int) {
    if (width == 0 || height == 0) {
      return
    }

  }
}
