package hela.eillance.viewer

import android.R
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
import hela.eillance.camera.databinding.FragmentViewerStreamBinding
import hela.eillance.ndkio.Config
import hela.eillance.ndkio.Device
import hela.eillance.ndkio.Profile
import hela.eillance.ndkio.Resolution

private const val RESOLUTION_SEPARATOR = "×"

class StreamFragment(internal val device: Device) : Fragment(),
  SurfaceHolder.Callback {

  private var _binding: FragmentViewerStreamBinding? = null
  private val binding get() = _binding!!
  private var profile: Profile = device.profile

  private val defaultCameraAdapter: ArrayAdapter<String> by lazy {
    ArrayAdapter(
      requireContext(),
      R.layout.simple_spinner_item,
      listOf(getString(hela.eillance.camera.R.string.fr_observer_operation_default_camera))
    )
  }

  private val defaultResolutionAdapter: ArrayAdapter<String> by lazy {
    ArrayAdapter(
      requireContext(),
      R.layout.simple_spinner_item,
      listOf(getString(hela.eillance.camera.R.string.fr_observer_operation_default_resolution))
    )
  }

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    _binding = FragmentViewerStreamBinding.inflate(inflater, container, false)

    binding.outputSurface.holder.addCallback(this)
    binding.selectCamera.adapter = defaultCameraAdapter
    binding.selectCameraResolution.adapter = defaultResolutionAdapter
    binding.selectCamera.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
      override fun onItemSelected(parent: AdapterView<*>, view: View?, position: Int, id: Long) {
        updateResolutionSpinner(position)
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
    (activity as? ViewerActivity)?.initDecoder(holder.surface)
  }

  override fun surfaceDestroyed(holder: SurfaceHolder) {
    (activity as? ViewerActivity)?.releaseDecoder()
  }

  override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
  }

  override fun onStart() {
    super.onStart()
    sendStreamRequest()
  }

  private fun sendStreamRequest() {
    val config = Config()
    val selectedCameraPosition = binding.selectCamera.selectedItemPosition
    if (selectedCameraPosition in profile.indices) {
      val camera = profile[selectedCameraPosition]
      config.video.cameraId = camera.id.toInt()

      val selectedResolutionPosition = binding.selectCameraResolution.selectedItemPosition
      if (selectedResolutionPosition in camera.resolutions.indices) {
        val resolution = camera.resolutions[selectedResolutionPosition]
        config.video.width = resolution.width
        config.video.height = resolution.height
      }
    }
    (activity as? ViewerActivity)?.sendStreamRequest(device.id, config)
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
    val cameraNames = profile.map { "${it.id}: ${it.name}" }
    updateCameraSpinner(cameraNames)
  }

  private fun updateCameraSpinner(cameraNames: List<String>) {
    if (cameraNames.isEmpty()) {
      Handler(Looper.getMainLooper()).post {
        binding.selectCamera.adapter = defaultCameraAdapter
      }
      return
    }

    val adapter = ArrayAdapter(
      requireContext(),
      R.layout.simple_spinner_item,
      cameraNames
    )
    Handler(Looper.getMainLooper()).post {
      binding.selectCamera.adapter = adapter
    }
  }

  private fun updateResolutionSpinner(cameraPosition: Int) {
    if (cameraPosition !in profile.indices) {
      Handler(Looper.getMainLooper()).post {
        binding.selectCameraResolution.adapter = defaultResolutionAdapter
      }
      return
    }

    val resolutions = profile[cameraPosition].resolutions
    val resolutionStrings = resolutions.map { it.toResolutionString() }
    val adapter = ArrayAdapter(
      requireContext(),
      R.layout.simple_spinner_item,
      resolutionStrings
    )
    Handler(Looper.getMainLooper()).post {
      binding.selectCameraResolution.adapter = adapter
    }
  }
  private fun Resolution.toResolutionString(): String {
    return "${this.width}$RESOLUTION_SEPARATOR${this.height}"
  }
}