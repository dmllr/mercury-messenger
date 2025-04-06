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

val AppIcons.Status5: ImageVector
    get() {
        if (_Status5 != null) {
            return _Status5!!
        }
        _Status5 = ImageVector.Builder(
            name = "Status5",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(12f, 2f)
                curveTo(6.47f, 2f, 2f, 6.47f, 2f, 12f)
                curveToRelative(0f, 5.53f, 4.47f, 10f, 10f, 10f)
                reflectiveCurveToRelative(10f, -4.47f, 10f, -10f)
                curveTo(22f, 6.47f, 17.53f, 2f, 12f, 2f)
                close()
                moveTo(12f, 20f)
                curveToRelative(-4.42f, 0f, -8f, -3.58f, -8f, -8f)
                curveToRelative(0f, -4.42f, 3.58f, -8f, 8f, -8f)
                reflectiveCurveToRelative(8f, 3.58f, 8f, 8f)
                curveTo(20f, 16.42f, 16.42f, 20f, 12f, 20f)
                close()
            }
        }.build()

        return _Status5!!
    }

@Suppress("ObjectPropertyName")
private var _Status5: ImageVector? = null

@Preview
@Composable
private fun Status5Preview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.Status5, contentDescription = null)
    }
}
