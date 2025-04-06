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

val AppIcons.StatusStandby: ImageVector
    get() {
        if (_StatusStandby != null) {
            return _StatusStandby!!
        }
        _StatusStandby = ImageVector.Builder(
            name = "StatusStandby",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(12f, 10f)
                curveTo(10.9f, 10f, 10f, 10.9f, 10f, 12f)
                curveToRelative(0f, 1.1f, 0.9f, 2f, 2f, 2f)
                reflectiveCurveToRelative(2f, -0.9f, 2f, -2f)
                curveTo(14f, 10.9f, 13.1f, 10f, 12f, 10f)
                close()
                moveTo(12f, 12f)
                curveToRelative(0f, 0f, 0f, 0f, 0f, 0f)
                curveTo(12f, 12f, 12f, 12f, 12f, 12f)
                close()
            }
        }.build()

        return _StatusStandby!!
    }

@Suppress("ObjectPropertyName")
private var _StatusStandby: ImageVector? = null

@Preview
@Composable
private fun StatusStandbyPreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.StatusStandby, contentDescription = null)
    }
}
