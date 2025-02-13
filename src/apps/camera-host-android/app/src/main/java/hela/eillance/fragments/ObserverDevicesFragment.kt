package hela.eillance.fragments

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
import hela.eillance.ObserverActivity
import hela.eillance.camera.R
import hela.eillance.camera.databinding.FragmentObserverDevicesBinding
import hela.eillance.ndkio.ClientInfo
import java.util.Vector

interface DevicesListAdapterEventListener {
  fun onItemClicked(device: ClientInfo)
}

class ObserverDevicesFragment : Fragment(), DevicesListAdapterEventListener {

  companion object {
    val instance = ObserverDevicesFragment()
  }

  private var _binding: FragmentObserverDevicesBinding? = null
  private val binding get() = _binding!!
  private val devicesListAdapter = DevicesListAdapter(this)

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    _binding = FragmentObserverDevicesBinding.inflate(inflater, container, false)
    binding.listDevices.layoutManager = LinearLayoutManager(requireContext())
    binding.listDevices.adapter = devicesListAdapter

    return binding.root
  }

  override fun onDestroyView() {
    super.onDestroyView()
    _binding = null
  }

  fun updateClientList(clients: List<ClientInfo>) {
    devicesListAdapter.update(clients)
  }

  class DevicesListAdapter(val listener: DevicesListAdapterEventListener) :
    RecyclerView.Adapter<DevicesListAdapter.ViewHolder>() {

    private val dataSet: Vector<ClientInfo> = Vector()

    fun update(clients: List<ClientInfo>) {
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
        .inflate(R.layout.fragment_observer_devices_item, viewGroup, false)
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

  override fun onItemClicked(device: ClientInfo) {
    (activity as ObserverActivity)
      .putFragment(ObserverOperationFragment(device))
  }

}
