package hela.eillance

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Bundle
import androidx.fragment.app.Fragment
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.TextView
import androidx.core.content.ContextCompat.getColor
import hela.eillance.camera.R
import hela.eillance.events.EventSource
import hela.eillance.events.StateIce
import hela.eillance.events.StateIceGathering
import hela.eillance.events.StateService
import hela.eillance.events.StateSignalling
import hela.eillance.events.getResourceString

class OperationFragment : Fragment() {

  private lateinit var iceGatheringState: TextView
  private lateinit var iceConnectionState: TextView
  private lateinit var signalingState: TextView
  private lateinit var communicationStatusState: TextView
  private lateinit var pingState: TextView
  private var defaultTextColor: Int = 0

  private val receiver = object : BroadcastReceiver() {
    override fun onReceive(ctx: Context?, intent: Intent?) {
      val sourceValue = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_source.get(), 0)
      val state = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_state.get(), 0)

      val source = EventSource.fromInt(sourceValue!!)
      val context = ctx!!

      when (source) {
        EventSource.GatheringState -> iceGatheringState.text =
          StateIceGathering.fromInt(state).getResourceString(context)
        EventSource.IceState -> iceConnectionState.text =
          StateIce.fromInt(state).getResourceString(context)
        EventSource.SignalingState -> signalingState.text =
          StateSignalling.fromInt(state).getResourceString(context)
        EventSource.ConnectionStatus -> communicationStatusState.text =
          StateService.fromInt(state).getResourceString(context)
        EventSource.Ping -> {
          pingState.apply {
            setTextColor(getColor(context, R.color.colorPositive))
            postDelayed({setTextColor(defaultTextColor)}, 333)
          }
        }
        EventSource.System -> {}
      }
    }
  }

  override fun onResume() {
    super.onResume()
    requireContext().registerReceiver(receiver, IntentFilter(Tune::BroadcastActionOnStateUpdate.get()),
      Context.RECEIVER_NOT_EXPORTED)
    updateStatuses()
  }

  override fun onPause() {
    super.onPause()
    requireContext().unregisterReceiver(receiver)
  }

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View? {
    return inflater.inflate(R.layout.fragment_operation, container, false)
  }

  override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
    super.onViewCreated(view, savedInstanceState)

    iceGatheringState = view.findViewById(R.id.ice_gathering_state)
    iceConnectionState = view.findViewById(R.id.ice_connection_state)
    signalingState = view.findViewById(R.id.signaling_state)
    communicationStatusState = view.findViewById(R.id.communication_status_state)
    pingState = view.findViewById(R.id.ping_state)
    defaultTextColor = pingState.currentTextColor

    val openPreferencesButton = view.findViewById<ImageButton>(R.id.btn_open_camera_preferences)
    openPreferencesButton.setOnClickListener {
      val capturePreferencesFragment = CapturePreferencesFragment()
      parentFragmentManager.beginTransaction()
        .add(R.id.content_view, capturePreferencesFragment)
        .addToBackStack(null)
        .commit()
    }

    view.postDelayed({updateStatuses()}, 2000)
  }

  private fun updateStatuses() {
    val status = getStatus()
    if (status != null) {
      val context = requireContext()
      iceGatheringState.text = status.iceGatheringState.getResourceString(context)
      iceConnectionState.text = status.iceState.getResourceString(context)
      signalingState.text = status.signallingState.getResourceString(context)
      communicationStatusState.text = status.connectionState.getResourceString(context)
    }
  }

  private fun getStatus(): Status? {
    val activity = requireActivity() as MainActivity
    val cameraService = activity.cameraService
    return cameraService?.getStatus()
  }


}