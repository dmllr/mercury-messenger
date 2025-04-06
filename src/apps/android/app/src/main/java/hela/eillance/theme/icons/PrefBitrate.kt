package hela.eillance.theme.icons

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp

val AppIcons.PrefBitrate: ImageVector
    get() {
        if (_PrefBitrate != null) {
            return _PrefBitrate!!
        }
        _PrefBitrate = ImageVector.Builder(
            name = "PrefBitrate",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(12f, 4f)
                curveTo(7.31f, 4f, 3.07f, 5.9f, 0f, 8.98f)
                lineTo(12f, 21f)
                lineTo(24f, 8.98f)
                curveTo(20.93f, 5.9f, 16.69f, 4f, 12f, 4f)
                close()
                moveTo(16.78f, 13.38f)
                curveTo(15.4f, 12.5f, 13.76f, 12f, 12f, 12f)
                curveToRelative(-1.76f, 0f, -3.4f, 0.5f, -4.78f, 1.38f)
                lineToRelative(-4.3f, -4.3f)
                curveTo(5.51f, 7.08f, 8.67f, 6f, 12f, 6f)
                reflectiveCurveToRelative(6.49f, 1.08f, 9.08f, 3.07f)
                lineTo(16.78f, 13.38f)
                close()
            }
        }.build()

        return _PrefBitrate!!
    }

@Suppress("ObjectPropertyName")
private var _PrefBitrate: ImageVector? = null

@Preview
@Composable
private fun PrefBitratePreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.PrefBitrate, contentDescription = null)
    }
}
