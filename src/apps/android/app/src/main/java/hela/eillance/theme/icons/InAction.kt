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

val AppIcons.InAction: ImageVector
    get() {
        if (_InAction != null) {
            return _InAction!!
        }
        _InAction = ImageVector.Builder(
            name = "InAction",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(
                fill = SolidColor(Color(0xFF000000)),
                fillAlpha = 0.3f,
                strokeAlpha = 0.3f
            ) {
                moveTo(12f, 20f)
                curveToRelative(-4.41f, 0f, -8f, -3.59f, -8f, -8f)
                reflectiveCurveToRelative(3.59f, -8f, 8f, -8f)
                reflectiveCurveToRelative(8f, 3.59f, 8f, 8f)
                reflectiveCurveTo(16.41f, 20f, 12f, 20f)
                moveTo(11f, 12f)
                lineToRelative(0f, 4f)
                horizontalLineToRelative(2f)
                lineToRelative(0f, -4f)
                horizontalLineToRelative(3f)
                lineToRelative(-4f, -4f)
                lineToRelative(-4f, 4f)
                horizontalLineTo(11f)
                close()
            }
            path(fill = SolidColor(Color(0xFF000000))) {
                moveTo(12f, 20f)
                curveToRelative(-4.41f, 0f, -8f, -3.59f, -8f, -8f)
                reflectiveCurveToRelative(3.59f, -8f, 8f, -8f)
                reflectiveCurveToRelative(8f, 3.59f, 8f, 8f)
                reflectiveCurveTo(16.41f, 20f, 12f, 20f)
                moveTo(12f, 22f)
                curveToRelative(5.52f, 0f, 10f, -4.48f, 10f, -10f)
                curveToRelative(0f, -5.52f, -4.48f, -10f, -10f, -10f)
                curveTo(6.48f, 2f, 2f, 6.48f, 2f, 12f)
                curveTo(2f, 17.52f, 6.48f, 22f, 12f, 22f)
                lineTo(12f, 22f)
                close()
                moveTo(11f, 12f)
                lineToRelative(0f, 4f)
                horizontalLineToRelative(2f)
                lineToRelative(0f, -4f)
                horizontalLineToRelative(3f)
                lineToRelative(-4f, -4f)
                lineToRelative(-4f, 4f)
                horizontalLineTo(11f)
                close()
            }
        }.build()

        return _InAction!!
    }

@Suppress("ObjectPropertyName")
private var _InAction: ImageVector? = null

@Preview
@Composable
private fun InActionPreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.InAction, contentDescription = null)
    }
}
