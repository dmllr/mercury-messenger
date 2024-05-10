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
import android.widget.TextView
import hela.eillance.camera.R
import hela.eillance.events.EventSource
import hela.eillance.events.StateIce
import hela.eillance.events.StateIceGathering
import hela.eillance.events.StateSignalling
import hela.eillance.events.getResourceString

class OperationFragment : Fragment() {

  private val receiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context?, intent: Intent?) {
      val sourceValue = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_source.get(), 0)
      val state = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_state.get(), 0)

      val source = EventSource.fromInt(sourceValue!!)

      when (source) {
        EventSource.GatheringState -> view?.findViewById<TextView>(R.id.ice_gathering_state)?.text =
          StateIceGathering.fromInt(state).getResourceString(activity!!)
        EventSource.IceState -> view?.findViewById<TextView>(R.id.ice_connection_state)?.text =
          StateIce.fromInt(state)?.getResourceString(activity!!)
        EventSource.SignalingState -> view?.findViewById<TextView>(R.id.signaling_state)?.text =
          StateSignalling.fromInt(state).getResourceString(activity!!)
        EventSource.System -> {}
        EventSource.ConnectionStatus -> {}
      }
    }
  }

  override fun onResume() {
    super.onResume()
    requireContext().registerReceiver(receiver, IntentFilter(Tune::BroadcastActionOnStateUpdate.get()),
      Context.RECEIVER_NOT_EXPORTED)
  }

  override fun onPause() {
    super.onPause()
    // Unregister the receiver
    requireContext().unregisterReceiver(receiver)
  }

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View? {
    return inflater.inflate(R.layout.fragment_operation, container, false)
  }

}