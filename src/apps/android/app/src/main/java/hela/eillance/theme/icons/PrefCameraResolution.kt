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

val AppIcons.PrefCameraResolution: ImageVector
    get() {
        if (_PrefCameraResolution != null) {
            return _PrefCameraResolution!!
        }
        _PrefCameraResolution = ImageVector.Builder(
            name = "PrefCameraResolution",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(20f, 4f)
                horizontalLineTo(4f)
                curveTo(2.9f, 4f, 2f, 4.9f, 2f, 6f)
                verticalLineToRelative(12f)
                curveToRelative(0f, 1.1f, 0.9f, 2f, 2f, 2f)
                horizontalLineToRelative(16f)
                curveToRelative(1.1f, 0f, 2f, -0.9f, 2f, -2f)
                verticalLineTo(6f)
                curveTo(22f, 4.9f, 21.1f, 4f, 20f, 4f)
                close()
                moveTo(4f, 6f)
                horizontalLineToRelative(2f)
                verticalLineToRelative(12f)
                horizontalLineTo(4f)
                verticalLineTo(6f)
                close()
                moveTo(20f, 18f)
                horizontalLineToRelative(-2f)
                verticalLineTo(6f)
                horizontalLineToRelative(2f)
                verticalLineTo(18f)
                close()
            }
        }.build()

        return _PrefCameraResolution!!
    }

@Suppress("ObjectPropertyName")
private var _PrefCameraResolution: ImageVector? = null

@Preview
@Composable
private fun PrefCameraResolutionPreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.PrefCameraResolution, contentDescription = null)
    }
}
