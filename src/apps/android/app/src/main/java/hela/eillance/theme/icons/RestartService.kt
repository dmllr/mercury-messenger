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

val AppIcons.RestartService: ImageVector
    get() {
        if (_RestartService != null) {
            return _RestartService!!
        }
        _RestartService = ImageVector.Builder(
            name = "RestartService",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(12f, 5f)
                verticalLineTo(2f)
                lineTo(8f, 6f)
                lineToRelative(4f, 4f)
                verticalLineTo(7f)
                curveToRelative(3.31f, 0f, 6f, 2.69f, 6f, 6f)
                curveToRelative(0f, 2.97f, -2.17f, 5.43f, -5f, 5.91f)
                verticalLineToRelative(2.02f)
                curveToRelative(3.95f, -0.49f, 7f, -3.85f, 7f, -7.93f)
                curveTo(20f, 8.58f, 16.42f, 5f, 12f, 5f)
                close()
            }
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(6f, 13f)
                curveToRelative(0f, -1.65f, 0.67f, -3.15f, 1.76f, -4.24f)
                lineTo(6.34f, 7.34f)
                curveTo(4.9f, 8.79f, 4f, 10.79f, 4f, 13f)
                curveToRelative(0f, 4.08f, 3.05f, 7.44f, 7f, 7.93f)
                verticalLineToRelative(-2.02f)
                curveTo(8.17f, 18.43f, 6f, 15.97f, 6f, 13f)
                close()
            }
        }.build()

        return _RestartService!!
    }

@Suppress("ObjectPropertyName")
private var _RestartService: ImageVector? = null

@Preview
@Composable
private fun RestartServicePreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.RestartService, contentDescription = null)
    }
}
