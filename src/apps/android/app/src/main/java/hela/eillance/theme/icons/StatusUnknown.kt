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

val AppIcons.StatusUnknown: ImageVector
    get() {
        if (_StatusUnknown != null) {
            return _StatusUnknown!!
        }
        _StatusUnknown = ImageVector.Builder(
            name = "StatusUnknown",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(17f, 1f)
                lineTo(7f, 1f)
                curveToRelative(-1.1f, 0f, -2f, 0.9f, -2f, 2f)
                verticalLineToRelative(18f)
                curveToRelative(0f, 1.1f, 0.9f, 2f, 2f, 2f)
                horizontalLineToRelative(10f)
                curveToRelative(1.1f, 0f, 2f, -0.9f, 2f, -2f)
                lineTo(19f, 3f)
                curveToRelative(0f, -1.1f, -0.9f, -2f, -2f, -2f)
                close()
                moveTo(17f, 19f)
                lineTo(7f, 19f)
                lineTo(7f, 5f)
                horizontalLineToRelative(10f)
                verticalLineToRelative(14f)
                close()
                moveTo(12f, 6.72f)
                curveToRelative(-1.96f, 0f, -3.5f, 1.52f, -3.5f, 3.47f)
                horizontalLineToRelative(1.75f)
                curveToRelative(0f, -0.93f, 0.82f, -1.75f, 1.75f, -1.75f)
                reflectiveCurveToRelative(1.75f, 0.82f, 1.75f, 1.75f)
                curveToRelative(0f, 1.75f, -2.63f, 1.57f, -2.63f, 4.45f)
                horizontalLineToRelative(1.76f)
                curveToRelative(0f, -1.96f, 2.62f, -2.19f, 2.62f, -4.45f)
                curveToRelative(0f, -1.96f, -1.54f, -3.47f, -3.5f, -3.47f)
                close()
                moveTo(11.12f, 15.52f)
                horizontalLineToRelative(1.76f)
                verticalLineToRelative(1.76f)
                horizontalLineToRelative(-1.76f)
                close()
            }
        }.build()

        return _StatusUnknown!!
    }

@Suppress("ObjectPropertyName")
private var _StatusUnknown: ImageVector? = null

@Preview
@Composable
private fun StatusUnknownPreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.StatusUnknown, contentDescription = null)
    }
}
