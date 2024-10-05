package hela.eillance.fragments

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Bundle
import android.view.Gravity
import androidx.fragment.app.Fragment
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.PopupWindow
import androidx.core.content.ContextCompat
import hela.eillance.MainActivity
import hela.eillance.ndkio.Status
import hela.eillance.Tune
import hela.eillance.camera.R
import hela.eillance.camera.databinding.FragmentOperationBinding
import hela.eillance.events.EventSource
import hela.eillance.events.StateService
import hela.eillance.events.SystemEvent
import hela.eillance.events.getResourceString

class OperationFragment : Fragment() {

  companion object {
    fun instance() = OperationFragment()
  }

  private lateinit var popupView: View
  private lateinit var popupWindow: PopupWindow
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

    // Create the popup window
    popupView = layoutInflater.inflate(R.layout.fragment_operation_status_popup, null)
    popupWindow = PopupWindow(
      popupView,
      ViewGroup.LayoutParams.WRAP_CONTENT,
      ViewGroup.LayoutParams.WRAP_CONTENT,
      true
    )

    // Bindings
    binding.communicationStatusState.setOnClickListener {
      showStatusPopup(binding.communicationStatusState)
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
      binding.communicationStatusState.text = status.connectionState.getResourceString(context)
    }
  }

  private fun showStatusPopup(anchorView: View) {
    // Show the popup window
    popupWindow.showAsDropDown(anchorView, 0, 0)
  }

  private fun getStatus(): Status? {
    val activity = requireActivity() as MainActivity
    val cameraService = activity.appEngineService
    return cameraService?.getStatus()
  }
}
