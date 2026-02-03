package hela.eillance

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.PowerManager
import androidx.core.content.ContextCompat

class PermissionManager(private val context: Context) {
  fun hasCallPermissions(): Boolean {
    val camera =
      ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED
    val audio =
      ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
    val phone = ContextCompat.checkSelfPermission(
      context,
      Manifest.permission.READ_PHONE_STATE
    ) == PackageManager.PERMISSION_GRANTED
    val manageCalls = ContextCompat.checkSelfPermission(
      context,
      Manifest.permission.MANAGE_OWN_CALLS
    ) == PackageManager.PERMISSION_GRANTED
    return camera && audio && phone && manageCalls
  }

  fun hasNotificationPermission(): Boolean {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      return ContextCompat.checkSelfPermission(
        context,
        Manifest.permission.POST_NOTIFICATIONS
      ) == PackageManager.PERMISSION_GRANTED
    }
    return true
  }

  fun hasBatteryExemption(): Boolean {
    val powerManager = context.getSystemService(Context.POWER_SERVICE) as PowerManager
    return powerManager.isIgnoringBatteryOptimizations(context.packageName)
  }
}
