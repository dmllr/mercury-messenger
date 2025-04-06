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

val AppIcons.PrefDefaultCamera: ImageVector
    get() {
        if (_PrefDefaultCamera != null) {
            return _PrefDefaultCamera!!
        }
        _PrefDefaultCamera = ImageVector.Builder(
            name = "PrefDefaultCamera",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(18f, 10.48f)
                verticalLineTo(6f)
                curveToRelative(0f, -1.1f, -0.9f, -2f, -2f, -2f)
                horizontalLineTo(4f)
                curveTo(2.9f, 4f, 2f, 4.9f, 2f, 6f)
                verticalLineToRelative(12f)
                curveToRelative(0f, 1.1f, 0.9f, 2f, 2f, 2f)
                horizontalLineToRelative(12f)
                curveToRelative(1.1f, 0f, 2f, -0.9f, 2f, -2f)
                verticalLineToRelative(-4.48f)
                lineToRelative(4f, 3.98f)
                verticalLineToRelative(-11f)
                lineTo(18f, 10.48f)
                close()
                moveTo(5f, 16f)
                lineToRelative(2.38f, -3.17f)
                lineTo(9f, 15f)
                lineToRelative(2.62f, -3.5f)
                lineTo(15f, 16f)
                horizontalLineTo(5f)
                close()
            }
        }.build()

        return _PrefDefaultCamera!!
    }

@Suppress("ObjectPropertyName")
private var _PrefDefaultCamera: ImageVector? = null

@Preview
@Composable
private fun PrefDefaultCameraPreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.PrefDefaultCamera, contentDescription = null)
    }
}
