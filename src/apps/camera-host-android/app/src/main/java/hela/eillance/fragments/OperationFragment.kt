package hela.eillance.fragments

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Bundle
import androidx.fragment.app.Fragment
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.content.ContextCompat
import hela.eillance.MainActivity
import hela.eillance.ndkio.Status
import hela.eillance.Tune
import hela.eillance.camera.R
import hela.eillance.camera.databinding.FragmentOperationBinding
import hela.eillance.events.EventSource
import hela.eillance.events.StateIce
import hela.eillance.events.StateIceGathering
import hela.eillance.events.StatePeerConnection
import hela.eillance.events.StateService
import hela.eillance.events.StateSignaling
import hela.eillance.events.SystemEvent
import hela.eillance.events.getResourceString

class OperationFragment : Fragment() {

  companion object {
    fun instance() = OperationFragment()
  }

  private var _binding: FragmentOperationBinding? = null
  private val binding get() = _binding!!

  private var defaultTextColor: Int = 0

  private val receiver = object : BroadcastReceiver() {
    override fun onReceive(ctx: Context?, intent: Intent?) {
      val sourceValue = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_source.get(), 0)
      val state = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_state.get(), 0)

      val source = EventSource.fromInt(sourceValue!!)
      val context = ctx!!

      when (source) {
        EventSource.GatheringState -> binding.iceGatheringState.text =
          StateIceGathering.fromInt(state).getResourceString(context)

        EventSource.IceState -> binding.iceConnectionState.text =
          StateIce.fromInt(state).getResourceString(context)

        EventSource.SignalingState -> binding.signalingState.text =
          StateSignaling.fromInt(state).getResourceString(context)

        EventSource.ConnectionStatus -> binding.communicationStatusState.text =
          StateService.fromInt(state).getResourceString(context)

        EventSource.PeerConnectionState -> binding.peerConnectionState.text =
          StatePeerConnection.fromInt(state).getResourceString(context)

        EventSource.Ping -> {
          binding.pingState.apply {
            setTextColor(ContextCompat.getColor(context, R.color.colorPositive))
            postDelayed({ setTextColor(defaultTextColor) }, 333)
          }
        }

        EventSource.System -> {
          if (state == SystemEvent.Started.value) {
            updateStatuses()
          }
        }
      }
    }
  }

  override fun onResume() {
    super.onResume()
    ContextCompat.registerReceiver(
      requireContext(), receiver, IntentFilter(Tune::BroadcastActionOnStateUpdate.get()),
      ContextCompat.RECEIVER_NOT_EXPORTED
    )
    updateStatuses()
  }

  override fun onPause() {
    super.onPause()
    requireContext().unregisterReceiver(receiver)
  }

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    _binding = FragmentOperationBinding.inflate(inflater, container, false)
    return binding.root
  }

  override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
    super.onViewCreated(view, savedInstanceState)

    defaultTextColor = binding.pingState.currentTextColor

    binding.btnOpenCameraPreferences.setOnClickListener {
      parentFragmentManager.beginTransaction()
        .add(R.id.content, CapturePreferencesFragment.instance())
        .addToBackStack(null)
        .commit()
    }
    binding.communicationStatusState.setOnClickListener {
      /// show popup
    }
    view.postDelayed({ updateStatuses() }, 2000)
  }

  override fun onDestroyView() {
    super.onDestroyView()
    _binding = null
  }

  private fun updateStatuses() {
    val status = getStatus()
    if (status != null) {
      val context = requireContext()
      binding.iceGatheringState.text = status.iceGatheringState.getResourceString(context)
      binding.iceConnectionState.text = status.iceState.getResourceString(context)
      binding.signalingState.text = status.signalingState.getResourceString(context)
      binding.communicationStatusState.text = status.connectionState.getResourceString(context)
      binding.peerConnectionState.text = status.peerConnectionState.getResourceString(context)
    }
  }

  private fun getStatus(): Status? {
    val activity = requireActivity() as MainActivity
    val cameraService = activity.appEngineService
    return cameraService?.getStatus()
  }
}
