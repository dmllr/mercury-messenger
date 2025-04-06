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

val AppIcons.PrefGroupAudioEncoding: ImageVector
    get() {
        if (_PrefGroupAudioEncoding != null) {
            return _PrefGroupAudioEncoding!!
        }
        _PrefGroupAudioEncoding = ImageVector.Builder(
            name = "PrefGroupAudioEncoding",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(7f, 24f)
                horizontalLineToRelative(2f)
                verticalLineToRelative(-2f)
                lineTo(7f, 22f)
                verticalLineToRelative(2f)
                close()
                moveTo(12f, 13f)
                curveToRelative(1.66f, 0f, 2.99f, -1.34f, 2.99f, -3f)
                lineTo(15f, 4f)
                curveToRelative(0f, -1.66f, -1.34f, -3f, -3f, -3f)
                reflectiveCurveTo(9f, 2.34f, 9f, 4f)
                verticalLineToRelative(6f)
                curveToRelative(0f, 1.66f, 1.34f, 3f, 3f, 3f)
                close()
                moveTo(11f, 24f)
                horizontalLineToRelative(2f)
                verticalLineToRelative(-2f)
                horizontalLineToRelative(-2f)
                verticalLineToRelative(2f)
                close()
                moveTo(15f, 24f)
                horizontalLineToRelative(2f)
                verticalLineToRelative(-2f)
                horizontalLineToRelative(-2f)
                verticalLineToRelative(2f)
                close()
                moveTo(19f, 10f)
                horizontalLineToRelative(-1.7f)
                curveToRelative(0f, 3f, -2.54f, 5.1f, -5.3f, 5.1f)
                reflectiveCurveTo(6.7f, 13f, 6.7f, 10f)
                lineTo(5f, 10f)
                curveToRelative(0f, 3.41f, 2.72f, 6.23f, 6f, 6.72f)
                lineTo(11f, 20f)
                horizontalLineToRelative(2f)
                verticalLineToRelative(-3.28f)
                curveToRelative(3.28f, -0.49f, 6f, -3.31f, 6f, -6.72f)
                close()
            }
        }.build()

        return _PrefGroupAudioEncoding!!
    }

@Suppress("ObjectPropertyName")
private var _PrefGroupAudioEncoding: ImageVector? = null

@Preview
@Composable
private fun PrefGroupAudioEncodingPreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.PrefGroupAudioEncoding, contentDescription = null)
    }
}
