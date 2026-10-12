package hela.eillance.service

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import hela.eillance.MessengerService

class BootReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    val action = intent.action
    if (action == Intent.ACTION_BOOT_COMPLETED || action == Intent.ACTION_MY_PACKAGE_REPLACED) {
      val serviceIntent = Intent(context, MessengerService::class.java)
      context.startForegroundService(serviceIntent)
    }
  }
}
