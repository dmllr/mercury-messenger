package hela.eillance.viewmodels

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel

class DebugViewModel(application: Application) : AndroidViewModel(application) {

  private var nativePtr: Long = 0

  init {
    nativePtr = nativeCreate()
    if (nativePtr == 0L) {
      Log.e("DebugViewModel", "Failed to create native DebugViewModel.")
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  fun eraseAllHistory() {
    if (nativePtr != 0L) nativeEraseHistory(nativePtr)
  }

  fun eraseAllContacts() {
    if (nativePtr != 0L) nativeEraseContacts(nativePtr)
  }

  fun revokeAllApprovals() {
    if (nativePtr != 0L) nativeRevokeApprovals(nativePtr)
  }

  private external fun nativeCreate(): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeEraseHistory(ptr: Long)
  private external fun nativeEraseContacts(ptr: Long)
  private external fun nativeRevokeApprovals(ptr: Long)
}
