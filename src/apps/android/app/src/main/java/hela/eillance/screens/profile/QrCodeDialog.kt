package hela.eillance.screens.profile

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.core.graphics.createBitmap
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel
import hela.eillance.camera.R
import hela.eillance.viewmodels.ContactsViewModel

@Composable
fun QrCodeDialog(
  profileId: String,
  contactsVM: ContactsViewModel,
  onDismiss: () -> Unit
) {
  var qrContent by remember { mutableStateOf<String?>(null) }

  LaunchedEffect(Unit) {
    val token = contactsVM.generateContactToken()
    qrContent = if (token != null) {
      "mercury:contact:$profileId?token=$token"
    } else {
      "mercury:contact:$profileId"
    }
  }

  Dialog(onDismissRequest = onDismiss) {
    Card(
      modifier = Modifier.padding(16.dp),
      shape = MaterialTheme.shapes.large
    ) {
      Column(
        modifier = Modifier.padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally
      ) {
        if (qrContent != null) {
          val qrBitmap = remember(qrContent) { generateQrCodeBitmap(qrContent!!) }
          Image(
            bitmap = qrBitmap,
            contentDescription = stringResource(id = R.string.profile_qr_code_description),
            modifier = Modifier.size(256.dp)
          )
        } else {
          CircularProgressIndicator(modifier = Modifier.size(256.dp))
        }
        Spacer(Modifier.height(16.dp))
        TextButton(onClick = onDismiss) { Text(stringResource(id = R.string.action_close)) }
      }
    }
  }
}

private fun generateQrCodeBitmap(content: String): androidx.compose.ui.graphics.ImageBitmap {
  val size = 512
  val hints = mapOf(
    EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.H,
    EncodeHintType.MARGIN to 1
  )
  val bitMatrix = QRCodeWriter().encode(content, BarcodeFormat.QR_CODE, size, size, hints)
  val pixels = IntArray(size * size)
  for (y in 0 until size) {
    for (x in 0 until size) {
      pixels[y * size + x] =
        if (bitMatrix[x, y]) android.graphics.Color.BLACK else android.graphics.Color.WHITE
    }
  }
  val bitmap = createBitmap(size, size)
  bitmap.setPixels(pixels, 0, size, 0, 0, size, size)
  return bitmap.asImageBitmap()
}
