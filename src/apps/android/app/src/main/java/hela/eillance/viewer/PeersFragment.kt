package hela.eillance.viewer

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.cardview.widget.CardView
import androidx.fragment.app.Fragment
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import hela.eillance.camera.R
import hela.eillance.camera.databinding.FragmentViewerPeersBinding
import hela.eillance.ndkio.Peer
import java.util.Vector

interface DevicesListAdapterEventListener {
  fun onItemClicked(peer: Peer)
}

class PeersFragment : Fragment(), DevicesListAdapterEventListener {

  companion object {
    val instance = PeersFragment()
  }

  private var _binding: FragmentViewerPeersBinding? = null
  private val binding get() = _binding!!
  private val devicesListAdapter = DevicesListAdapter(this)

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    _binding = FragmentViewerPeersBinding.inflate(inflater, container, false)
    binding.listDevices.layoutManager = LinearLayoutManager(requireContext())
    binding.listDevices.adapter = devicesListAdapter

    return binding.root
  }

  override fun onDestroyView() {
    super.onDestroyView()
    _binding = null
  }

  fun updatePeerList(clients: List<Peer>) {
    devicesListAdapter.update(clients)
  }

  class DevicesListAdapter(val listener: DevicesListAdapterEventListener) :
    RecyclerView.Adapter<DevicesListAdapter.ViewHolder>() {

    private val dataSet: Vector<Peer> = Vector()

    fun update(clients: List<Peer>) {
      Handler(Looper.getMainLooper()).post {
        val oldData = dataSet

        val diffCallback = object : DiffUtil.Callback() {
          override fun getOldListSize() = oldData.size
          override fun getNewListSize() = clients.size

          override fun areItemsTheSame(oldItemPosition: Int, newItemPosition: Int): Boolean {
            return oldData[oldItemPosition].id == clients[newItemPosition].id
          }

          override fun areContentsTheSame(oldItemPosition: Int, newItemPosition: Int): Boolean {
            return oldData[oldItemPosition] == clients[newItemPosition]
          }
        }

        val diffResult = DiffUtil.calculateDiff(diffCallback)
        dataSet.clear()
        dataSet.addAll(clients)
        diffResult.dispatchUpdatesTo(this)
      }
    }

    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
      val card: CardView = view.findViewById(R.id.card_device)
      val deviceName: TextView = view.findViewById(R.id.text_device_name)
    }

    override fun onCreateViewHolder(viewGroup: ViewGroup, viewType: Int): ViewHolder {
      val view = LayoutInflater.from(viewGroup.context)
        .inflate(R.layout.fragment_viewer_peers_peer, viewGroup, false)
      return ViewHolder(view)
    }

    override fun onBindViewHolder(viewHolder: ViewHolder, position: Int) {
      val device = dataSet[position]
      viewHolder.deviceName.text = device.name
      viewHolder.card.setOnClickListener {
        listener.onItemClicked(device)
      }
    }

    override fun getItemCount(): Int {
      return dataSet.size
    }
  }

  override fun onItemClicked(peer: Peer) {
    (activity as ViewerActivity)
      .putFragment(StreamFragment(peer), StreamFragment::class.java.canonicalName)
  }

}
