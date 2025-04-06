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

val AppIcons.CameraStream: ImageVector
    get() {
        if (_CameraStream != null) {
            return _CameraStream!!
        }
        _CameraStream = ImageVector.Builder(
            name = "CameraStream",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(12f, 14f)
                moveToRelative(-3.2f, 0f)
                arcToRelative(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, 6.4f, 0f)
                arcToRelative(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, -6.4f, 0f)
            }
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(16f, 3.33f)
                curveToRelative(2.58f, 0f, 4.67f, 2.09f, 4.67f, 4.67f)
                horizontalLineTo(22f)
                curveToRelative(0f, -3.31f, -2.69f, -6f, -6f, -6f)
                verticalLineToRelative(1.33f)
                moveTo(16f, 6f)
                curveToRelative(1.11f, 0f, 2f, 0.89f, 2f, 2f)
                horizontalLineToRelative(1.33f)
                curveToRelative(0f, -1.84f, -1.49f, -3.33f, -3.33f, -3.33f)
                verticalLineTo(6f)
            }
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(17f, 9f)
                curveToRelative(0f, -1.11f, -0.89f, -2f, -2f, -2f)
                lineTo(15f, 4f)
                lineTo(9f, 4f)
                lineTo(7.17f, 6f)
                lineTo(4f, 6f)
                curveToRelative(-1.1f, 0f, -2f, 0.9f, -2f, 2f)
                verticalLineToRelative(12f)
                curveToRelative(0f, 1.1f, 0.9f, 2f, 2f, 2f)
                horizontalLineToRelative(16f)
                curveToRelative(1.1f, 0f, 2f, -0.9f, 2f, -2f)
                lineTo(22f, 9f)
                horizontalLineToRelative(-5f)
                close()
                moveTo(12f, 19f)
                curveToRelative(-2.76f, 0f, -5f, -2.24f, -5f, -5f)
                reflectiveCurveToRelative(2.24f, -5f, 5f, -5f)
                reflectiveCurveToRelative(5f, 2.24f, 5f, 5f)
                reflectiveCurveToRelative(-2.24f, 5f, -5f, 5f)
                close()
            }
        }.build()

        return _CameraStream!!
    }

@Suppress("ObjectPropertyName")
private var _CameraStream: ImageVector? = null

@Preview
@Composable
private fun CameraStreamPreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.CameraStream, contentDescription = null)
    }
}
