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

val AppIcons.CapturePreferences: ImageVector
    get() {
        if (_CapturePreferences != null) {
            return _CapturePreferences!!
        }
        _CapturePreferences = ImageVector.Builder(
            name = "CapturePreferences",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(20f, 3f)
                horizontalLineTo(4f)
                curveTo(2.89f, 3f, 2f, 3.89f, 2f, 5f)
                verticalLineToRelative(12f)
                curveToRelative(0f, 1.1f, 0.89f, 2f, 2f, 2f)
                horizontalLineToRelative(4f)
                verticalLineToRelative(2f)
                horizontalLineToRelative(8f)
                verticalLineToRelative(-2f)
                horizontalLineToRelative(4f)
                curveToRelative(1.1f, 0f, 2f, -0.9f, 2f, -2f)
                verticalLineTo(5f)
                curveTo(22f, 3.89f, 21.1f, 3f, 20f, 3f)
                close()
                moveTo(20f, 17f)
                horizontalLineTo(4f)
                verticalLineTo(5f)
                horizontalLineToRelative(16f)
                verticalLineTo(17f)
                close()
            }
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(6f, 8.25f)
                horizontalLineToRelative(8f)
                verticalLineToRelative(1.5f)
                horizontalLineToRelative(-8f)
                close()
            }
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(16.5f, 9.75f)
                lineToRelative(1.5f, 0f)
                lineToRelative(0f, -1.5f)
                lineToRelative(-1.5f, 0f)
                lineToRelative(0f, -1.25f)
                lineToRelative(-1.5f, 0f)
                lineToRelative(0f, 4f)
                lineToRelative(1.5f, 0f)
                close()
            }
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(10f, 12.25f)
                horizontalLineToRelative(8f)
                verticalLineToRelative(1.5f)
                horizontalLineToRelative(-8f)
                close()
            }
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(7.5f, 15f)
                lineToRelative(1.5f, 0f)
                lineToRelative(0f, -4f)
                lineToRelative(-1.5f, 0f)
                lineToRelative(0f, 1.25f)
                lineToRelative(-1.5f, 0f)
                lineToRelative(0f, 1.5f)
                lineToRelative(1.5f, 0f)
                close()
            }
        }.build()

        return _CapturePreferences!!
    }

@Suppress("ObjectPropertyName")
private var _CapturePreferences: ImageVector? = null

@Preview
@Composable
private fun CapturePreferencesPreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.CapturePreferences, contentDescription = null)
    }
}
