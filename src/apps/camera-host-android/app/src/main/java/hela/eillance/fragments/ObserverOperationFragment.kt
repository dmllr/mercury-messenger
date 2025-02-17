package hela.eillance.fragments

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.SurfaceHolder
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.ArrayAdapter
import androidx.fragment.app.Fragment
import hela.eillance.ObserverActivity
import hela.eillance.camera.R
import hela.eillance.camera.databinding.FragmentObserverOperationBinding
import hela.eillance.ndkio.ClientInfo
import hela.eillance.ndkio.Profile

class ObserverOperationFragment(val device: ClientInfo)
  : Fragment(), SurfaceHolder.Callback
{
  private var _binding: FragmentObserverOperationBinding? = null
  private val binding get() = _binding!!
  private var profile: Profile = device.profile

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    _binding = FragmentObserverOperationBinding.inflate(inflater, container, false)
    val defaultCameraAdapter: ArrayAdapter<String> =
      ArrayAdapter(requireContext(), android.R.layout.simple_spinner_item,
        listOf(getString(R.string.fr_observer_operation_default_camera)))
    val defaultResolutionAdapter: ArrayAdapter<String> =
      ArrayAdapter(requireContext(), android.R.layout.simple_spinner_item,
        listOf(getString(R.string.fr_observer_operation_default_resolution)))

    binding.outputSurface.holder.addCallback(this)
    binding.selectCamera.adapter = defaultCameraAdapter
    binding.selectCameraResolution.adapter = defaultResolutionAdapter
    binding.selectCamera.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
      override fun onItemSelected(parent: AdapterView<*>, view: View?, position: Int, id: Long) {
        if (position >= profile.size) {
          Handler(Looper.getMainLooper()).post {
          binding.selectCameraResolution.adapter = defaultResolutionAdapter
            }
          return
        }
        val adapter: ArrayAdapter<String> =
          ArrayAdapter(requireContext(), android.R.layout.simple_spinner_item,
            profile[position].resolutions.map { "${it.width}×${it.height}" })
        Handler(Looper.getMainLooper()).post {
          binding.selectCameraResolution.adapter = adapter
        }
      }
      override fun onNothingSelected(parent: AdapterView<*>) {
      }
    }

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
    (activity as ObserverActivity).sendStreamRequest(device.id)
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

      binding.outputSurface.requestLayout()
    }
  }

  fun onDeviceProfileReceived(profile: Profile) {
    this.profile = profile
    val adapter: ArrayAdapter<String> =
      ArrayAdapter(requireContext(), android.R.layout.simple_spinner_item,
        this.profile.map { "${it.id}: ${it.name}" })
    Handler(Looper.getMainLooper()).post {
      binding.selectCamera.adapter = adapter
    }
  }

}
