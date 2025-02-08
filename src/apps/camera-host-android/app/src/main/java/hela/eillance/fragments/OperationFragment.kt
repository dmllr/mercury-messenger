package hela.eillance.fragments

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.recyclerview.widget.DefaultItemAnimator
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import hela.eillance.PerformerActivity
import hela.eillance.Tune
import hela.eillance.camera.R
import hela.eillance.camera.databinding.FragmentOperationBinding
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.PeerStateLink
import hela.eillance.ndkio.ServiceConnectionState
import java.util.Vector

class OperationFragment : Fragment() {

  companion object {
    fun instance() = OperationFragment()
  }

  private var _binding: FragmentOperationBinding? = null
  private val binding get() = _binding!!
  private val connectionsListAdapter = ConnectionsListAdapter()

  private val receiver = object : BroadcastReceiver() {
    override fun onReceive(ctx: Context?, intent: Intent?) {
      val action = intent?.action
      when (action) {
        Tune::BroadcastActionOnStateUpdate.get() -> {
          onStateUpdateIntentReceived(intent, ctx)
        }
      }
    }
  }

  private fun onStateUpdateIntentReceived(intent: Intent?, ctx: Context?) {
    val sourceValue = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_source.get(), 0)
    val peerId = intent?.getStringExtra(Tune::BroadcastActionOnStateUpdate_peer_id.get())
    val state = intent?.getIntExtra(Tune::BroadcastActionOnStateUpdate_state.get(), 0)
    onStateUpdate(peerId!!, EventSource.fromInt(sourceValue!!), state!!, ctx!!)
  }

  private fun onStateUpdate(
    peerId: String,
    source: EventSource,
    state: Int,
    context: Context
  ) {
    when (source) {
      EventSource.System -> {
        binding.txCommunicationState.text =
          ServiceConnectionState.fromInt(state).getResourceString(context)
      }

      else -> {
        connectionsListAdapter.update(peerId, source, state)
      }
    }
  }

  override fun onResume() {
    super.onResume()
    val intentFilter = IntentFilter(Tune::BroadcastActionOnStateUpdate.get())
    LocalBroadcastManager.getInstance(requireContext())
      .registerReceiver(receiver, intentFilter)
    updateServiceConnectionState()
  }

  override fun onPause() {
    super.onPause()
    LocalBroadcastManager.getInstance(requireContext()).unregisterReceiver(receiver)
  }

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    _binding = FragmentOperationBinding.inflate(inflater, container, false)

    binding.lsConnectedClients.layoutManager = LinearLayoutManager(requireContext())
    binding.lsConnectedClients.adapter = connectionsListAdapter

    return binding.root
  }

  override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
    super.onViewCreated(view, savedInstanceState)

    // disable blinking on recycler view items updates
    val animator = binding.lsConnectedClients.itemAnimator as? DefaultItemAnimator
    animator?.supportsChangeAnimations = false

    view.postDelayed({ updateServiceConnectionState() }, 200)
  }

  override fun onDestroyView() {
    super.onDestroyView()
    _binding = null
  }

  private fun updateServiceConnectionState() {
    activity ?: return
    val appEngineService = (activity as PerformerActivity).performerService ?: return
    val state = appEngineService.getServiceConnectionState()
    binding.txCommunicationState.text = state.getResourceString(requireContext())
  }

  class ConnectionsListAdapter :
    RecyclerView.Adapter<ConnectionsListAdapter.ViewHolder>() {

    // Store the peerId and its state info
    private val dataSet: Vector<String> = Vector()
    private val stateMap: MutableMap<String, MutableMap<EventSource, Int>> = mutableMapOf()

    fun add(peerId: String) {
      dataSet.add(peerId)
      notifyItemInserted(dataSet.size - 1)
    }

    fun remove(peerId: String) {
      val position = dataSet.indexOf(peerId)
      if (position == -1) return
      dataSet.removeAt(position)
      stateMap.remove(peerId)
      notifyItemRemoved(position)
    }

    fun update(peerId: String, source: EventSource, state: Int) {
      if (!dataSet.contains(peerId)) {
        add(peerId)
      }
      // Ensure that stateMap for peerId contains a MutableMap for EventSource states
      stateMap.getOrPut(peerId) { mutableMapOf() }[source] = state
      val position = dataSet.indexOf(peerId)
      notifyItemChanged(position)
    }

    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
      val clientName: TextView = view.findViewById(R.id.txClientName)
      val peerConnectionState: TextView = view.findViewById(R.id.peerConnectionState)
      val pingState: TextView = view.findViewById(R.id.pingState)
    }

    override fun onCreateViewHolder(viewGroup: ViewGroup, viewType: Int): ViewHolder {
      val view = LayoutInflater.from(viewGroup.context)
        .inflate(R.layout.fragment_operation_connections_item, viewGroup, false)
      return ViewHolder(view)
    }

    override fun onBindViewHolder(viewHolder: ViewHolder, position: Int) {
      val peerId = dataSet[position]
      viewHolder.clientName.text = peerId

      stateMap[peerId]?.forEach { (source, state) ->
        val context = viewHolder.itemView.context
        when (source) {
          EventSource.PeerConnectionState -> viewHolder.peerConnectionState.text =
            PeerStateLink.fromInt(state).getResourceString(context)

          EventSource.Ping -> {
            viewHolder.pingState.apply {
              setTextColor(ContextCompat.getColor(context, R.color.colorPositive))
              postDelayed({ setTextColor(0x00) }, 333)
            }
          }

          else -> {}
        }
      }
    }

    override fun getItemCount() = dataSet.size
  }

}
