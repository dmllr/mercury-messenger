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
import hela.eillance.camera.databinding.FragmentOperationBinding
import hela.eillance.events.EventSource
import hela.eillance.events.StateService
import hela.eillance.events.SystemEvent
import hela.eillance.events.getResourceString

class OperationFragment : Fragment() {

  companion object {
    fun instance() = OperationFragment()
  }

  private var _binding: FragmentOperationBinding? = null
  private val binding get() = _binding!!

  private val receiver = object : BroadcastReceiver() {
    override fun onReceive(ctx: Context?, intent: Intent?) {
      val sourceValue = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_source.get(), 0)
      val state = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_state.get(), 0)

      val source = EventSource.fromInt(sourceValue!!)
      val context = ctx!!

      when (source) {
        EventSource.ConnectionStatus -> binding.communicationStatusState.text =
          StateService.fromInt(state).getResourceString(context)

        EventSource.System -> {
          if (state == SystemEvent.Started.value) {
            updateStatuses()
          }
        }

        EventSource.PeerConnectionState -> {}
        EventSource.GatheringState -> {}
        EventSource.IceState -> {}
        EventSource.SignalingState -> {}
        EventSource.Ping -> {}
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

    view.postDelayed({ updateStatuses() }, 2000)
  }

  override fun onDestroyView() {
    super.onDestroyView()
    _binding = null
  }

  private fun updateStatuses() {
    if (activity == null)
      return

    val status = getStatus()
    if (status != null) {
      val context = requireContext()
      binding.communicationStatusState.text = status.connectionState.getResourceString(context)
    }
  }

  private fun getStatus(): Status? {
    val activity = requireActivity() as MainActivity
    val cameraService = activity.appEngineService
    return cameraService?.getStatus()
  }
}
