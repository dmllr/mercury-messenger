package hela.eillance.viewer

import android.os.Bundle
import android.view.Surface
import androidx.activity.OnBackPressedCallback
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import hela.eillance.camera.R
import hela.eillance.camera.databinding.ActivityViewerBinding
import hela.eillance.ndkio.Config
import hela.eillance.ndkio.Device
import hela.eillance.ndkio.EventSource
import hela.eillance.ndkio.Profile
import hela.eillance.ndkio.interfaces.IViewerNativeBridge

class ViewerActivity : AppCompatActivity(), IViewerNativeBridge {
  private lateinit var binding: ActivityViewerBinding

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    enableEdgeToEdge()
    binding = ActivityViewerBinding.inflate(layoutInflater)
    setContentView(binding.root)
    ViewCompat.setOnApplyWindowInsetsListener(binding.main) { v, insets ->
      val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
      v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
      insets
    }
    setSupportActionBar(binding.toolbar)

    initEngine(this)

    supportFragmentManager.beginTransaction()
      .replace(R.id.content, PeersFragment.instance)
      .commit()

    supportFragmentManager.addOnBackStackChangedListener {
      if (supportFragmentManager.backStackEntryCount > 0) { // show back button
        supportActionBar!!.setDisplayHomeAsUpEnabled(true)
        binding.toolbar.setNavigationOnClickListener {
          onBackPressedDispatcher.onBackPressed()
        }
      } else { //show hamburger
        supportActionBar!!.setDisplayHomeAsUpEnabled(false)
      }
    }

    onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
      override fun handleOnBackPressed() {
        if (!popFragment()) {
          if (!isEnabled) {
            isEnabled = true
          }
          finish()
        }
      }
    })
  }

  fun putFragment(fragment: Fragment, tag: String? = null) {
    supportFragmentManager.beginTransaction()
      .add(R.id.content, fragment, tag)
      .addToBackStack(BACKSTACK)
      .commit()

    triggerUIUpdatesOnFragmentNavigation(supportFragmentManager, +1)
  }

  private fun popFragment(): Boolean {
    val c = supportFragmentManager.backStackEntryCount
    if (c == 0)
      return false

    supportFragmentManager.popBackStack()

    triggerUIUpdatesOnFragmentNavigation(supportFragmentManager, -1)

    return true
  }

  private fun triggerUIUpdatesOnFragmentNavigation(fm: FragmentManager, direction: Int) {
    val c = fm.backStackEntryCount + direction
    val actionBar = supportActionBar!!
    actionBar.setDisplayHomeAsUpEnabled(c > 0)
    actionBar.setDisplayShowHomeEnabled(c > 0)
  }

  private external fun initEngine(nativeBridge: IViewerNativeBridge)
  external fun initDecoder(surface: Surface)
  external fun releaseDecoder()
  external fun sendStreamRequest(clientId: String, config: Config)

  companion object {
    const val BACKSTACK = "fragments_backstack"
    init {
      System.loadLibrary("lib-viewer-service-bridge")
    }
  }

  override fun onStatusUpdate(peerId: String, source: Int, status: Int) {
    val fragment = supportFragmentManager.findFragmentByTag(
      StreamFragment::class.java.canonicalName)
        as StreamFragment?
    assert(peerId == (fragment?.device?.id ?: ""))

    fragment?.onStatusUpdate(EventSource.fromInt(source!!), status)
  }

  override fun onStreamResolutionChange(width: Int, height: Int) {
    val fragment = supportFragmentManager.findFragmentByTag(
      StreamFragment::class.java.canonicalName)
        as StreamFragment?
    fragment?.onStreamResolutionChange(width, height)
  }

  override fun onDeviceProfileReceived(peerId: String, profile: Profile) {
    val fragment = supportFragmentManager.findFragmentByTag(
      StreamFragment::class.java.canonicalName)
        as StreamFragment?
    assert(peerId == (fragment?.device?.id ?: ""))
    fragment?.onDeviceProfileReceived(profile)
  }

  override fun onClientListUpdate(clients: List<Device>) {
    PeersFragment.instance.updateClientList(clients)
  }

}
