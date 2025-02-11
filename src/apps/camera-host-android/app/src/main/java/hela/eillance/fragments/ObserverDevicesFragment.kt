package hela.eillance.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import hela.eillance.camera.databinding.FragmentObserverDevicesBinding
import hela.eillance.camera.databinding.FragmentObserverOperationBinding

class ObserverDevicesFragment : Fragment() {

  companion object {
    fun instance() = ObserverDevicesFragment()
  }

  private var _binding: FragmentObserverDevicesBinding? = null
  private val binding get() = _binding!!

  override fun onCreateView(
    inflater: LayoutInflater, container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View {
    _binding = FragmentObserverDevicesBinding.inflate(inflater, container, false)

    return binding.root
  }

  override fun onDestroyView() {
    super.onDestroyView()
    _binding = null
  }

}
