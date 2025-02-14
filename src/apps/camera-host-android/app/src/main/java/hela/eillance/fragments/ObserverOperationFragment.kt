package hela.eillance.fragments

import android.os.Bundle
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
import hela.eillance.ndkio.Camera
import hela.eillance.ndkio.ClientInfo
import hela.eillance.ndkio.Profile
import hela.eillance.ndkio.Resolution

class ObserverOperationFragment(val device: ClientInfo)
  : Fragment(),
  SurfaceHolder.Callback
{
  private var _binding: FragmentObserverOperationBinding? = null
  private val binding get() = _binding!!
  private inner class CameraSpinnerAdapter(cameras: List<Camera>)
    : ArrayAdapter<String>(requireContext(), android.R.layout.simple_spinner_item) {
    init {
      add(getString(R.string.fr_observer_operation_default_camera))
      addAll(
        cameras.map { it.id })
    }
  }
  private inner class CameraResolutionSpinnerAdapter(resolutions: List<Resolution>)
    : ArrayAdapter<String>(requireContext(), android.R.layout.simple_spinner_item) {
    init {
      add(getString(R.string.fr_observer_operation_default_resolution))
      addAll(
        resolutions.map { "${it.width}×${it.height}" })
    }
  }

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    _binding = FragmentObserverOperationBinding.inflate(inflater, container, false)
    val defaultResolutionAdapter: ArrayAdapter<String> =
      ArrayAdapter(requireContext(), android.R.layout.simple_spinner_item,
        listOf(getString(R.string.fr_observer_operation_default_resolution)))
    val cameraSpinnerAdapter = CameraSpinnerAdapter(device.profile)

    binding.outputSurface.holder.addCallback(this)
    binding.selectCamera.adapter = cameraSpinnerAdapter
    binding.selectCameraResolution.adapter = defaultResolutionAdapter
    binding.selectCamera.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
      override fun onItemSelected(parent: AdapterView<*>, view: View?, position: Int, id: Long) {
        if (position == 0) {
          binding.selectCameraResolution.adapter = defaultResolutionAdapter
        } else {
          binding.selectCameraResolution.adapter = CameraResolutionSpinnerAdapter(device.profile[position - 1].resolutions)
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

      binding.outputSurface.requestLayout() // Ensure UI updates
    }
  }

  fun onDeviceProfileReceived(profile: Profile) {
    TODO("Not yet implemented")
  }

}
