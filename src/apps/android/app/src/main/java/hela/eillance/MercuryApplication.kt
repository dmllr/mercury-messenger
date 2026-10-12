package hela.eillance

import android.app.Application

class MercuryApplication : Application() {
  override fun onCreate() {
    super.onCreate()
    System.loadLibrary("lib-client-service-bridge")
  }

  companion object {
    var isAppInForeground: Boolean = false
  }
}
