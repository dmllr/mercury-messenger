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

val AppIcons.StartService: ImageVector
    get() {
        if (_StartService != null) {
            return _StartService!!
        }
        _StartService = ImageVector.Builder(
            name = "StartService",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f,
            autoMirror = true
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(20f, 18f)
                curveToRelative(1.1f, 0f, 1.99f, -0.9f, 1.99f, -2f)
                lineTo(22f, 6f)
                curveToRelative(0f, -1.11f, -0.9f, -2f, -2f, -2f)
                lineTo(4f, 4f)
                curveToRelative(-1.11f, 0f, -2f, 0.89f, -2f, 2f)
                verticalLineToRelative(10f)
                curveToRelative(0f, 1.1f, 0.89f, 2f, 2f, 2f)
                lineTo(0f, 18f)
                verticalLineToRelative(2f)
                horizontalLineToRelative(24f)
                verticalLineToRelative(-2f)
                horizontalLineToRelative(-4f)
                close()
                moveTo(13f, 14.47f)
                verticalLineToRelative(-2.19f)
                curveToRelative(-2.78f, 0f, -4.61f, 0.85f, -6f, 2.72f)
                curveToRelative(0.56f, -2.67f, 2.11f, -5.33f, 6f, -5.87f)
                lineTo(13f, 7f)
                lineToRelative(4f, 3.73f)
                lineToRelative(-4f, 3.74f)
                close()
            }
        }.build()

        return _StartService!!
    }

@Suppress("ObjectPropertyName")
private var _StartService: ImageVector? = null

@Preview
@Composable
private fun StartServicePreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.StartService, contentDescription = null)
    }
}
