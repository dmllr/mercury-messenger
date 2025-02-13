package hela.eillance.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.SurfaceHolder
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import hela.eillance.ObserverActivity
import hela.eillance.camera.databinding.FragmentObserverOperationBinding
import hela.eillance.ndkio.ClientInfo

class ObserverOperationFragment(val client: ClientInfo) : Fragment(), SurfaceHolder.Callback {

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
    (activity as ObserverActivity).initDecoder(holder.surface)
  }

  override fun surfaceDestroyed(holder: SurfaceHolder) {
    (activity as ObserverActivity).releaseDecoder()
  }

  override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
  }

  override fun onStart() {
    super.onStart()
    sendStreamRequest()
  }

  private fun sendStreamRequest() {
    (activity as ObserverActivity).sendStreamRequest(client.id)
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

}
