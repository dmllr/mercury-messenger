package hela.eillance

import android.os.Bundle
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import hela.eillance.camera.R
import hela.eillance.camera.databinding.ActivityObserverBinding
import hela.eillance.fragments.ObserverDevicesFragment

class ObserverActivity : AppCompatActivity()
{
  private lateinit var binding: ActivityObserverBinding

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    enableEdgeToEdge()
    binding = ActivityObserverBinding.inflate(layoutInflater)
    setContentView(binding.root)
    ViewCompat.setOnApplyWindowInsetsListener(binding.main) { v, insets ->
      val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
      v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
      insets
    }

    supportFragmentManager.beginTransaction()
      .replace(R.id.content, ObserverDevicesFragment.instance())
      .commit()
  }
}
