package hela.eillance.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.SurfaceHolder
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import hela.eillance.ObserverActivity
import hela.eillance.camera.databinding.FragmentObserverOperationBinding

class ObserverOperationFragment : Fragment(), SurfaceHolder.Callback {

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
    val a = activity as ObserverActivity
    a.initDecoder(holder.surface)
  }

  override fun surfaceDestroyed(holder: SurfaceHolder) {
    val a = activity as ObserverActivity
    a.releaseDecoder()
  }

  override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
  }

  fun onStreamResolutionChange(width: Int, height: Int) {
    if (width == 0 || height == 0) {
      return
    }

    binding.outputSurface.post {
      val surfaceWidth = binding.outputSurface.width
      val newHeight = surfaceWidth * height / width

      binding.outputSurface.layoutParams = binding.outputSurface.layoutParams.apply {
        this.height = newHeight
      }

      binding.outputSurface.requestLayout() // Ensure UI updates
    }
  }

  companion object {
    fun new() = ObserverOperationFragment()
  }

}
