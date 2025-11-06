package hela.eillance.utils

import android.graphics.BitmapFactory
import android.util.Base64
import android.util.Log
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap

@Composable
fun rememberImageBitmapFromBase64(base64: String?): ImageBitmap? {
    return remember(base64) {
        if (base64.isNullOrBlank()) {
            null
        } else {
            try {
                val decodedBytes = Base64.decode(base64, Base64.NO_WRAP)
                BitmapFactory.decodeByteArray(decodedBytes, 0, decodedBytes.size)?.asImageBitmap()
            } catch (e: IllegalArgumentException) {
                // Handle invalid Base64 string
                Log.e("ImageBitmap", "Failed to decode base64 string", e)
                null
            }
        }
    }
}

