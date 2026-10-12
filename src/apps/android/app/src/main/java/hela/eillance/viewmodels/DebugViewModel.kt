package hela.eillance.viewmodels

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import hela.eillance.ndkio.DebugConnectionInfo
import hela.eillance.ndkio.DebugPendingAction
import hela.eillance.ndkio.DebugUnreadInfo

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

  fun getPendingActions(): List<DebugPendingAction> {
    return if (nativePtr != 0L) nativeGetPendingActions(nativePtr) else emptyList()
  }

  fun getConnectionInfo(): List<DebugConnectionInfo> {
    return if (nativePtr != 0L) nativeGetConnectionInfo(nativePtr) else emptyList()
  }

  fun getUnreadInfo(): List<DebugUnreadInfo> {
    return if (nativePtr != 0L) nativeGetUnreadInfo(nativePtr) else emptyList()
  }

  private external fun nativeCreate(): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeEraseHistory(ptr: Long)
  private external fun nativeEraseContacts(ptr: Long)
  private external fun nativeRevokeApprovals(ptr: Long)

  private external fun nativeGetPendingActions(ptr: Long): List<DebugPendingAction>
  private external fun nativeGetConnectionInfo(ptr: Long): List<DebugConnectionInfo>
  private external fun nativeGetUnreadInfo(ptr: Long): List<DebugUnreadInfo>
}
