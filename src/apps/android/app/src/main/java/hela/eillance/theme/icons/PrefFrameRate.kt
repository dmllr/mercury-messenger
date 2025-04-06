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

val AppIcons.PrefFrameRate: ImageVector
    get() {
        if (_PrefFrameRate != null) {
            return _PrefFrameRate!!
        }
        _PrefFrameRate = ImageVector.Builder(
            name = "PrefFrameRate",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(9f, 3f)
                lineTo(7.17f, 5f)
                lineTo(4f, 5f)
                curveToRelative(-1.1f, 0f, -2f, 0.9f, -2f, 2f)
                verticalLineToRelative(12f)
                curveToRelative(0f, 1.1f, 0.9f, 2f, 2f, 2f)
                horizontalLineToRelative(16f)
                curveToRelative(1.1f, 0f, 2f, -0.9f, 2f, -2f)
                lineTo(22f, 7f)
                curveToRelative(0f, -1.1f, -0.9f, -2f, -2f, -2f)
                horizontalLineToRelative(-3.17f)
                lineTo(15f, 3f)
                lineTo(9f, 3f)
                close()
                moveTo(12f, 18f)
                curveToRelative(-2.76f, 0f, -5f, -2.24f, -5f, -5f)
                reflectiveCurveToRelative(2.24f, -5f, 5f, -5f)
                reflectiveCurveToRelative(5f, 2.24f, 5f, 5f)
                reflectiveCurveToRelative(-2.24f, 5f, -5f, 5f)
                close()
            }
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(12f, 17f)
                lineToRelative(1.25f, -2.75f)
                lineTo(16f, 13f)
                lineToRelative(-2.75f, -1.25f)
                lineTo(12f, 9f)
                lineToRelative(-1.25f, 2.75f)
                lineTo(8f, 13f)
                lineToRelative(2.75f, 1.25f)
                close()
            }
        }.build()

        return _PrefFrameRate!!
    }

@Suppress("ObjectPropertyName")
private var _PrefFrameRate: ImageVector? = null

@Preview
@Composable
private fun PrefFrameRatePreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.PrefFrameRate, contentDescription = null)
    }
}
