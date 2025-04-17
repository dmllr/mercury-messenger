package hela.eillance.theme.icons

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.materialIcon
import androidx.compose.material.icons.materialPath
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp

val AppIcons.Signout: ImageVector
    get() {
        if (_signout != null) {
            return _signout!!
        }
        _signout = materialIcon(name = "Filled.signout") {
            materialPath {
                moveTo(10.09f, 15.59f)
                lineTo(11.5f, 17.0f)
                lineToRelative(5.0f, -5.0f)
                lineToRelative(-5.0f, -5.0f)
                lineToRelative(-1.41f, 1.41f)
                lineTo(12.67f, 11.0f)
                horizontalLineTo(3.0f)
                verticalLineToRelative(2.0f)
                horizontalLineToRelative(9.67f)
                lineToRelative(-2.58f, 2.59f)
                close()
                moveTo(19.0f, 3.0f)
                horizontalLineTo(5.0f)
                curveToRelative(-1.11f, 0.0f, -2.0f, 0.9f, -2.0f, 2.0f)
                verticalLineToRelative(4.0f)
                horizontalLineToRelative(2.0f)
                verticalLineTo(5.0f)
                horizontalLineToRelative(14.0f)
                verticalLineToRelative(14.0f)
                horizontalLineTo(5.0f)
                verticalLineToRelative(-4.0f)
                horizontalLineTo(3.0f)
                verticalLineToRelative(4.0f)
                curveToRelative(0.0f, 1.1f, 0.89f, 2.0f, 2.0f, 2.0f)
                horizontalLineToRelative(14.0f)
                curveToRelative(1.1f, 0.0f, 2.0f, -0.9f, 2.0f, -2.0f)
                verticalLineTo(5.0f)
                curveToRelative(0.0f, -1.1f, -0.9f, -2.0f, -2.0f, -2.0f)
                close()
            }
        }
        return _signout!!
    }

@Suppress("ObjectPropertyName")
private var _signout: ImageVector? = null

@Preview
@Composable
private fun SwitchRolePreview() {
    Box(modifier = Modifier.padding(12.dp)) {
        Image(imageVector = AppIcons.Signout, contentDescription = null)
    }
}
