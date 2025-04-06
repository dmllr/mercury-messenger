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

val AppIcons.FullscreenExit: ImageVector
    get() {
        if (_FullscreenExit != null) {
            return _FullscreenExit!!
        }
        _FullscreenExit = ImageVector.Builder(
            name = "FullscreenExit",
            defaultWidth = 24.dp,
            defaultHeight = 24.dp,
            viewportWidth = 24f,
            viewportHeight = 24f
        ).apply {
            path(fill = SolidColor(Color(0xFFFFFFFF))) {
                moveTo(5f, 16f)
                horizontalLineToRelative(3f)
                verticalLineToRelative(3f)
                horizontalLineToRelative(2f)
                verticalLineToRelative(-5f)
                lineTo(5f, 14f)
                verticalLineToRelative(2f)
                close()
                moveTo(5f, 8f)
                horizontalLineToRelative(5f)
                lineTo(10f, 3f)
                lineTo(8f, 3f)
                verticalLineToRelative(3f)
                lineTo(5f, 6f)
                verticalLineToRelative(2f)
                close()
                moveTo(14f, 21f)
                horizontalLineToRelative(2f)
                verticalLineToRelative(-3f)
                horizontalLineToRelative(3f)
                verticalLineToRelative(-2f)
                horizontalLineToRelative(-5f)
                verticalLineToRelative(5f)
                close()
                moveTo(16f, 3f)
                horizontalLineToRelative(-2f)
                verticalLineToRelative(5f)
                horizontalLineToRelative(5f)
                lineTo(19f, 6f)
                horizontalLineToRelative(-3f)
                lineTo(16f, 3f)
                close()
            }
        }.build()

        return _FullscreenExit!!
    }

@Suppress("ObjectPropertyName")
private var _FullscreenExit: ImageVector? = null

@Preview
@Composable
private fun FullscreenExitPreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.FullscreenExit, contentDescription = null)
    }
}
