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

val AppIcons.SurfaceRotate: ImageVector
    get() {
        if (_SurfaceRotate != null) {
            return _SurfaceRotate!!
        }
        _SurfaceRotate = ImageVector.Builder(
            name = "SurfaceRotate",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(4f, 7.59f)
                lineToRelative(5f, -5f)
                curveToRelative(0.78f, -0.78f, 2.05f, -0.78f, 2.83f, 0f)
                lineTo(20.24f, 11f)
                horizontalLineToRelative(-2.83f)
                lineTo(10.4f, 4f)
                lineTo(5.41f, 9f)
                horizontalLineTo(8f)
                verticalLineToRelative(2f)
                horizontalLineTo(2f)
                verticalLineTo(5f)
                horizontalLineToRelative(2f)
                verticalLineTo(7.59f)
                close()
                moveTo(20f, 19f)
                horizontalLineToRelative(2f)
                verticalLineToRelative(-6f)
                horizontalLineToRelative(-6f)
                verticalLineToRelative(2f)
                horizontalLineToRelative(2.59f)
                lineToRelative(-4.99f, 5f)
                lineToRelative(-7.01f, -7f)
                horizontalLineTo(3.76f)
                lineToRelative(8.41f, 8.41f)
                curveToRelative(0.78f, 0.78f, 2.05f, 0.78f, 2.83f, 0f)
                lineToRelative(5f, -5f)
                verticalLineTo(19f)
                close()
            }
        }.build()

        return _SurfaceRotate!!
    }

@Suppress("ObjectPropertyName")
private var _SurfaceRotate: ImageVector? = null

@Preview
@Composable
private fun SurfaceRotatePreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.SurfaceRotate, contentDescription = null)
    }
}
