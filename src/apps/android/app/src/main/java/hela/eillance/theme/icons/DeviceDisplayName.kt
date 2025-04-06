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

val AppIcons.DeviceDisplayName: ImageVector
    get() {
        if (_DeviceDisplayName != null) {
            return _DeviceDisplayName!!
        }
        _DeviceDisplayName = ImageVector.Builder(
            name = "DeviceDisplayName",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(18.41f, 5.8f)
                lineTo(17.2f, 4.59f)
                curveToRelative(-0.78f, -0.78f, -2.05f, -0.78f, -2.83f, 0f)
                lineToRelative(-2.68f, 2.68f)
                lineTo(3f, 15.96f)
                verticalLineTo(20f)
                horizontalLineToRelative(4.04f)
                lineToRelative(8.74f, -8.74f)
                lineToRelative(2.63f, -2.63f)
                curveToRelative(0.79f, -0.78f, 0.79f, -2.05f, 0f, -2.83f)
                close()
                moveTo(6.21f, 18f)
                horizontalLineTo(5f)
                verticalLineToRelative(-1.21f)
                lineToRelative(8.66f, -8.66f)
                lineToRelative(1.21f, 1.21f)
                lineTo(6.21f, 18f)
                close()
                moveTo(11f, 20f)
                lineToRelative(4f, -4f)
                horizontalLineToRelative(6f)
                verticalLineToRelative(4f)
                horizontalLineTo(11f)
                close()
            }
        }.build()

        return _DeviceDisplayName!!
    }

@Suppress("ObjectPropertyName")
private var _DeviceDisplayName: ImageVector? = null

@Preview
@Composable
private fun DeviceDisplayNamePreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.DeviceDisplayName, contentDescription = null)
    }
}
