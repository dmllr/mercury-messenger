package hela.eillance.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathFillType
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.unit.dp

object AppIcons {

  val Camera: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Camera",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Inner circle (lens)
          moveTo(12f, 12f)
          arcTo(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, 15.2f, 12f)
          arcTo(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 8.8f)
          arcTo(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, 8.8f, 12f)
          arcTo(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 15.2f)
          close()
        }
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Camera body
          moveTo(9f, 2f)
          lineTo(7.17f, 4f)
          lineTo(4f, 4f)
          curveTo(2.9f, 4f, 2f, 4.9f, 2f, 6f)
          lineTo(2f, 18f)
          curveTo(2f, 19.1f, 2.9f, 20f, 4f, 20f)
          lineTo(20f, 20f)
          curveTo(21.1f, 20f, 22f, 19.1f, 22f, 18f)
          lineTo(22f, 6f)
          curveTo(22f, 4.9f, 21.1f, 4f, 20f, 4f)
          lineTo(16.83f, 4f)
          lineTo(15f, 2f)
          lineTo(9f, 2f)
          close()

          // Camera outer circle
          moveTo(12f, 17f)
          curveTo(9.24f, 17f, 7f, 14.76f, 7f, 12f)
          curveTo(7f, 9.24f, 9.24f, 7f, 12f, 7f)
          curveTo(14.76f, 7f, 17f, 9.24f, 17f, 12f)
          curveTo(17f, 14.76f, 14.76f, 17f, 12f, 17f)
          close()
        }
      }.build()
    }

  val CameraStream: ImageVector
    get() {
      return ImageVector.Builder(
        name = "CameraStream",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Inner circle (lens)
          moveTo(12f, 14f)
          arcTo(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, 15.2f, 14f)
          arcTo(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 10.8f)
          arcTo(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, 8.8f, 14f)
          arcTo(3.2f, 3.2f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 17.2f)
          close()
        }
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Streaming signal (top arcs)
          moveTo(16f, 3.33f)
          curveTo(18.58f, 3.33f, 20.67f, 5.42f, 20.67f, 8f)
          lineTo(22f, 8f)
          curveTo(22f, 4.69f, 19.31f, 2f, 16f, 2f)
          lineTo(16f, 3.33f)

          moveTo(16f, 6f)
          curveTo(17.11f, 6f, 18f, 6.89f, 18f, 8f)
          lineTo(19.33f, 8f)
          curveTo(19.33f, 6.16f, 17.84f, 4.67f, 16f, 4.67f)
          lineTo(16f, 6f)
        }
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Camera body
          moveTo(17f, 9f)
          curveTo(17f, 7.89f, 16.11f, 7f, 15f, 7f)
          lineTo(15f, 4f)
          lineTo(9f, 4f)
          lineTo(7.17f, 6f)
          lineTo(4f, 6f)
          curveTo(2.9f, 6f, 2f, 6.9f, 2f, 8f)
          lineTo(2f, 20f)
          curveTo(2f, 21.1f, 2.9f, 22f, 4f, 22f)
          lineTo(20f, 22f)
          curveTo(21.1f, 22f, 22f, 21.1f, 22f, 20f)
          lineTo(22f, 9f)
          lineTo(17f, 9f)
          close()

          moveTo(12f, 19f)
          curveTo(9.24f, 19f, 7f, 16.76f, 7f, 14f)
          curveTo(7f, 11.24f, 9.24f, 9f, 12f, 9f)
          curveTo(14.76f, 9f, 17f, 11.24f, 17f, 14f)
          curveTo(17f, 16.76f, 14.76f, 19f, 12f, 19f)
          close()
        }
      }.build()
    }

  /**
   * Refresh/Connecting icon - designed to be rotated during animation
   */
  val Refresh: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Refresh",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        // Circular arrow path
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 2.0f,
          strokeLineCap = StrokeCap.Round,
          strokeLineJoin = StrokeJoin.Round,
          stroke = SolidColor(Color.Black),
          strokeLineMiter = 4.0f,
          fillAlpha = 0.0f,
        ) {
          // Circular connecting animation arrow
          moveTo(20f, 12f)
          arcTo(8f, 8f, 0f, isMoreThanHalf = false, isPositiveArc = false, 12f, 4f)
          arcTo(8f, 8f, 0f, isMoreThanHalf = false, isPositiveArc = false, 4f, 12f)
          arcTo(8f, 8f, 0f, isMoreThanHalf = false, isPositiveArc = false, 12f, 20f)
          arcTo(8f, 8f, 0f, isMoreThanHalf = false, isPositiveArc = false, 16.5f, 16.5f)
        }

        // Arrow head
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Arrow tip
          moveTo(20f, 8f)
          lineTo(20f, 12f)
          lineTo(16f, 12f)
          close()
        }
      }.build()
    }

  val CapturePreferences: ImageVector
    get() {
      return ImageVector.Builder(
        name = "CapturePreferences",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Screen/monitor outline
          moveTo(20f, 3f)
          lineTo(4f, 3f)
          curveTo(2.89f, 3f, 2f, 3.89f, 2f, 5f)
          lineTo(2f, 17f)
          curveTo(2f, 18.1f, 2.89f, 19f, 4f, 19f)
          lineTo(8f, 19f)
          lineTo(8f, 21f)
          lineTo(16f, 21f)
          lineTo(16f, 19f)
          lineTo(20f, 19f)
          curveTo(21.1f, 19f, 22f, 18.1f, 22f, 17f)
          lineTo(22f, 5f)
          curveTo(22f, 3.89f, 21.1f, 3f, 20f, 3f)
          close()

          moveTo(20f, 17f)
          lineTo(4f, 17f)
          lineTo(4f, 5f)
          lineTo(20f, 5f)
          lineTo(20f, 17f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // First horizontal line
          moveTo(6f, 8.25f)
          lineTo(14f, 8.25f)
          lineTo(14f, 9.75f)
          lineTo(6f, 9.75f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // First bracket (right side)
          moveTo(16.5f, 9.75f)
          lineTo(18f, 9.75f)
          lineTo(18f, 8.25f)
          lineTo(16.5f, 8.25f)
          lineTo(16.5f, 7f)
          lineTo(15f, 7f)
          lineTo(15f, 11f)
          lineTo(16.5f, 11f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Second horizontal line
          moveTo(10f, 12.25f)
          lineTo(18f, 12.25f)
          lineTo(18f, 13.75f)
          lineTo(10f, 13.75f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Second bracket (left side)
          moveTo(7.5f, 15f)
          lineTo(9f, 15f)
          lineTo(9f, 11f)
          lineTo(7.5f, 11f)
          lineTo(7.5f, 12.25f)
          lineTo(6f, 12.25f)
          lineTo(6f, 13.75f)
          lineTo(7.5f, 13.75f)
          close()
        }
      }.build()
    }

  val DeviceDisplayName: ImageVector
    get() {
      return ImageVector.Builder(
        name = "DeviceDisplayName",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Pencil icon with text line
          moveTo(18.41f, 5.8f)
          lineTo(17.2f, 4.59f)
          curveTo(16.42f, 3.81f, 15.15f, 3.81f, 14.37f, 4.59f)
          lineTo(11.69f, 7.27f)
          lineTo(3f, 15.96f)
          lineTo(3f, 20f)
          lineTo(7.04f, 20f)
          lineTo(15.78f, 11.26f)
          lineTo(18.41f, 8.63f)
          curveTo(19.2f, 7.85f, 19.2f, 6.58f, 18.41f, 5.8f)
          close()

          moveTo(6.21f, 18f)
          lineTo(5f, 18f)
          lineTo(5f, 16.79f)
          lineTo(13.66f, 8.13f)
          lineTo(14.87f, 9.34f)
          lineTo(6.21f, 18f)
          close()

          moveTo(11f, 20f)
          lineTo(15f, 16f)
          lineTo(21f, 16f)
          lineTo(21f, 20f)
          lineTo(11f, 20f)
          close()
        }
      }.build()
    }

  val Failure: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Failure",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White.copy(alpha = 0.3f)),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Inner semi-transparent circle
          moveTo(12f, 4f)
          curveTo(7.58f, 4f, 4f, 7.58f, 4f, 12f)
          curveTo(4f, 16.42f, 7.58f, 20f, 12f, 20f)
          curveTo(16.42f, 20f, 20f, 16.42f, 20f, 12f)
          curveTo(20f, 7.58f, 16.42f, 4f, 12f, 4f)

          moveTo(13f, 17f)
          lineTo(11f, 17f)
          lineTo(11f, 15f)
          lineTo(13f, 15f)
          lineTo(13f, 17f)

          moveTo(13f, 13f)
          lineTo(11f, 13f)
          lineTo(11f, 7f)
          lineTo(13f, 7f)
          lineTo(13f, 13f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Outer circle with exclamation mark
          moveTo(11.99f, 2f)
          curveTo(6.47f, 2f, 2f, 6.48f, 2f, 12f)
          curveTo(2f, 17.52f, 6.47f, 22f, 11.99f, 22f)
          curveTo(17.52f, 22f, 22f, 17.52f, 22f, 12f)
          curveTo(22f, 6.48f, 17.52f, 2f, 11.99f, 2f)

          moveTo(12f, 20f)
          curveTo(7.58f, 20f, 4f, 16.42f, 4f, 12f)
          curveTo(4f, 7.58f, 7.58f, 4f, 12f, 4f)
          curveTo(16.42f, 4f, 20f, 7.58f, 20f, 12f)
          curveTo(20f, 16.42f, 16.42f, 20f, 12f, 20f)
          close()

          moveTo(11f, 15f)
          lineTo(13f, 15f)
          lineTo(13f, 17f)
          lineTo(11f, 17f)
          close()

          moveTo(11f, 7f)
          lineTo(13f, 7f)
          lineTo(13f, 13f)
          lineTo(11f, 13f)
          close()
        }
      }.build()
    }

  val Fullscreen: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Fullscreen",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Expand arrows pointing outward
          moveTo(7f, 14f)
          lineTo(5f, 14f)
          lineTo(5f, 19f)
          lineTo(10f, 19f)
          lineTo(10f, 17f)
          lineTo(7f, 17f)
          lineTo(7f, 14f)
          close()

          moveTo(5f, 10f)
          lineTo(7f, 10f)
          lineTo(7f, 7f)
          lineTo(10f, 7f)
          lineTo(10f, 5f)
          lineTo(5f, 5f)
          lineTo(5f, 10f)
          close()

          moveTo(17f, 17f)
          lineTo(14f, 17f)
          lineTo(14f, 19f)
          lineTo(19f, 19f)
          lineTo(19f, 14f)
          lineTo(17f, 14f)
          lineTo(17f, 17f)
          close()

          moveTo(14f, 5f)
          lineTo(14f, 7f)
          lineTo(17f, 7f)
          lineTo(17f, 10f)
          lineTo(19f, 10f)
          lineTo(19f, 5f)
          lineTo(14f, 5f)
          close()
        }
      }.build()
    }

  val FullscreenExit: ImageVector
    get() {
      return ImageVector.Builder(
        name = "FullscreenExit",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Collapse arrows pointing inward
          moveTo(5f, 16f)
          lineTo(8f, 16f)
          lineTo(8f, 19f)
          lineTo(10f, 19f)
          lineTo(10f, 14f)
          lineTo(5f, 14f)
          lineTo(5f, 16f)
          close()

          moveTo(5f, 8f)
          lineTo(10f, 8f)
          lineTo(10f, 3f)
          lineTo(8f, 3f)
          lineTo(8f, 6f)
          lineTo(5f, 6f)
          lineTo(5f, 8f)
          close()

          moveTo(14f, 21f)
          lineTo(16f, 21f)
          lineTo(16f, 18f)
          lineTo(19f, 18f)
          lineTo(19f, 16f)
          lineTo(14f, 16f)
          lineTo(14f, 21f)
          close()

          moveTo(16f, 3f)
          lineTo(14f, 3f)
          lineTo(14f, 8f)
          lineTo(19f, 8f)
          lineTo(19f, 6f)
          lineTo(16f, 6f)
          lineTo(16f, 3f)
          close()
        }
      }.build()
    }

  val InAction: ImageVector
    get() {
      return ImageVector.Builder(
        name = "InAction",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White.copy(alpha = 0.3f)),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Semi-transparent inner circle with arrow
          moveTo(12f, 20f)
          curveTo(7.59f, 20f, 4f, 16.41f, 4f, 12f)
          curveTo(4f, 7.59f, 7.59f, 4f, 12f, 4f)
          curveTo(16.41f, 4f, 20f, 7.59f, 20f, 12f)
          curveTo(20f, 16.41f, 16.41f, 20f, 12f, 20f)

          moveTo(11f, 12f)
          lineTo(11f, 16f)
          lineTo(13f, 16f)
          lineTo(13f, 12f)
          lineTo(16f, 12f)
          lineTo(12f, 8f)
          lineTo(8f, 12f)
          lineTo(11f, 12f)
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Outer circle with full arrow
          moveTo(12f, 20f)
          curveTo(7.59f, 20f, 4f, 16.41f, 4f, 12f)
          curveTo(4f, 7.59f, 7.59f, 4f, 12f, 4f)
          curveTo(16.41f, 4f, 20f, 7.59f, 20f, 12f)
          curveTo(20f, 16.41f, 16.41f, 20f, 12f, 20f)

          moveTo(12f, 22f)
          curveTo(17.52f, 22f, 22f, 17.52f, 22f, 12f)
          curveTo(22f, 6.48f, 17.52f, 2f, 12f, 2f)
          curveTo(6.48f, 2f, 2f, 6.48f, 2f, 12f)
          curveTo(2f, 17.52f, 6.48f, 22f, 12f, 22f)
          lineTo(12f, 22f)
          close()

          moveTo(11f, 12f)
          lineTo(11f, 16f)
          lineTo(13f, 16f)
          lineTo(13f, 12f)
          lineTo(16f, 12f)
          lineTo(12f, 8f)
          lineTo(8f, 12f)
          lineTo(11f, 12f)
          close()
        }
      }.build()
    }

  val PrefBitrate: ImageVector
    get() {
      return ImageVector.Builder(
        name = "PrefBitrate",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(12f, 4f)
          curveTo(7.31f, 4f, 3.07f, 5.9f, 0f, 8.98f)
          lineTo(12f, 21f)
          lineTo(24f, 8.98f)
          curveTo(20.93f, 5.9f, 16.69f, 4f, 12f, 4f)
          close()
          moveTo(16.78f, 13.38f)
          curveTo(15.4f, 12.5f, 13.76f, 12f, 12f, 12f)
          curveTo(10.24f, 12f, 8.6f, 12.5f, 7.22f, 13.38f)
          lineTo(2.92f, 9.08f)
          curveTo(5.51f, 7.08f, 8.67f, 6f, 12f, 6f)
          curveTo(15.33f, 6f, 18.49f, 7.08f, 21.08f, 9.07f)
          lineTo(16.78f, 13.38f)
          close()
        }
      }.build()
    }

  val PrefCameraResolution: ImageVector
    get() {
      return ImageVector.Builder(
        name = "PrefCameraResolution",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(20f, 4f)
          horizontalLineTo(4f)
          curveTo(2.9f, 4f, 2f, 4.9f, 2f, 6f)
          verticalLineTo(18f)
          curveTo(2f, 19.1f, 2.9f, 20f, 4f, 20f)
          horizontalLineTo(20f)
          curveTo(21.1f, 20f, 22f, 19.1f, 22f, 18f)
          verticalLineTo(6f)
          curveTo(22f, 4.9f, 21.1f, 4f, 20f, 4f)
          close()
          moveTo(4f, 6f)
          horizontalLineTo(6f)
          verticalLineTo(18f)
          horizontalLineTo(4f)
          verticalLineTo(6f)
          close()
          moveTo(20f, 18f)
          horizontalLineTo(18f)
          verticalLineTo(6f)
          horizontalLineTo(20f)
          verticalLineTo(18f)
          close()
        }
      }.build()
    }

  val PrefDefaultCamera: ImageVector
    get() {
      return ImageVector.Builder(
        name = "PrefDefaultCamera",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(18f, 10.48f)
          verticalLineTo(6f)
          curveTo(18f, 4.9f, 17.1f, 4f, 16f, 4f)
          horizontalLineTo(4f)
          curveTo(2.9f, 4f, 2f, 4.9f, 2f, 6f)
          verticalLineTo(18f)
          curveTo(2f, 19.1f, 2.9f, 20f, 4f, 20f)
          horizontalLineTo(16f)
          curveTo(17.1f, 20f, 18f, 19.1f, 18f, 18f)
          verticalLineTo(13.52f)
          lineTo(22f, 17.5f)
          verticalLineTo(6.5f)
          lineTo(18f, 10.48f)
          close()
          moveTo(5f, 16f)
          lineTo(7.38f, 12.83f)
          lineTo(9f, 15f)
          lineTo(11.62f, 11.5f)
          lineTo(15f, 16f)
          horizontalLineTo(5f)
          close()
        }
      }.build()
    }

  val PrefFrameRate: ImageVector
    get() {
      return ImageVector.Builder(
        name = "PrefFrameRate",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(9f, 3f)
          lineTo(7.17f, 5f)
          horizontalLineTo(4f)
          curveTo(2.9f, 5f, 2f, 5.9f, 2f, 7f)
          verticalLineTo(19f)
          curveTo(2f, 20.1f, 2.9f, 21f, 4f, 21f)
          horizontalLineTo(20f)
          curveTo(21.1f, 21f, 22f, 20.1f, 22f, 19f)
          lineTo(22f, 7f)
          curveTo(22f, 5.9f, 21.1f, 5f, 20f, 5f)
          horizontalLineTo(16.83f)
          lineTo(15f, 3f)
          horizontalLineTo(9f)
          close()
          moveTo(12f, 18f)
          curveTo(9.24f, 18f, 7f, 15.76f, 7f, 13f)
          curveTo(7f, 10.24f, 9.24f, 8f, 12f, 8f)
          curveTo(14.76f, 8f, 17f, 10.24f, 17f, 13f)
          curveTo(17f, 15.76f, 14.76f, 18f, 12f, 18f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(12f, 17f)
          lineTo(13.25f, 14.25f)
          lineTo(16f, 13f)
          lineTo(13.25f, 11.75f)
          lineTo(12f, 9f)
          lineTo(10.75f, 11.75f)
          lineTo(8f, 13f)
          lineTo(10.75f, 14.25f)
          close()
        }
      }.build()
    }

  val PrefAudioEncoding: ImageVector
    get() {
      return ImageVector.Builder(
        name = "PrefAudioEncoding",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(7f, 24f)
          horizontalLineTo(9f)
          verticalLineTo(22f)
          horizontalLineTo(7f)
          verticalLineTo(24f)
          close()
          moveTo(12f, 13f)
          curveTo(13.66f, 13f, 14.99f, 11.66f, 14.99f, 10f)
          lineTo(15f, 4f)
          curveTo(15f, 2.34f, 13.66f, 1f, 12f, 1f)
          curveTo(10.34f, 1f, 9f, 2.34f, 9f, 4f)
          verticalLineTo(10f)
          curveTo(9f, 11.66f, 10.34f, 13f, 12f, 13f)
          close()
          moveTo(11f, 24f)
          horizontalLineTo(13f)
          verticalLineTo(22f)
          horizontalLineTo(11f)
          verticalLineTo(24f)
          close()
          moveTo(15f, 24f)
          horizontalLineTo(17f)
          verticalLineTo(22f)
          horizontalLineTo(15f)
          verticalLineTo(24f)
          close()
          moveTo(19f, 10f)
          horizontalLineTo(17.3f)
          curveTo(17.3f, 13f, 14.76f, 15.1f, 12f, 15.1f)
          curveTo(9.24f, 15.1f, 6.7f, 13f, 6.7f, 10f)
          horizontalLineTo(5f)
          curveTo(5f, 13.41f, 7.72f, 16.23f, 11f, 16.72f)
          verticalLineTo(20f)
          horizontalLineTo(13f)
          verticalLineTo(16.72f)
          curveTo(16.28f, 16.23f, 19f, 13.41f, 19f, 10f)
          close()
        }
      }.build()
    }

  val PrefGroupDevice: ImageVector
    get() {
      return ImageVector.Builder(
        name = "PrefGroupDevice",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(17f, 1.01f)
          lineTo(7f, 1f)
          curveTo(5.9f, 1f, 5f, 1.9f, 5f, 3f)
          verticalLineTo(21f)
          curveTo(5f, 22.1f, 5.9f, 23f, 7f, 23f)
          horizontalLineTo(17f)
          curveTo(18.1f, 23f, 19f, 22.1f, 19f, 21f)
          verticalLineTo(3f)
          curveTo(19f, 1.9f, 18.1f, 1.01f, 17f, 1.01f)
          close()
          moveTo(17f, 19f)
          horizontalLineTo(7f)
          verticalLineTo(5f)
          horizontalLineTo(17f)
          verticalLineTo(19f)
          close()
        }
      }.build()
    }

  val PrefGroupVideoEncoding: ImageVector
    get() {
      return ImageVector.Builder(
        name = "PrefGroupVideoEncoding",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(20f, 3f)
          horizontalLineTo(4f)
          curveTo(2.89f, 3f, 2f, 3.89f, 2f, 5f)
          verticalLineTo(17f)
          curveTo(2f, 18.1f, 2.89f, 19f, 4f, 19f)
          horizontalLineTo(8f)
          verticalLineTo(21f)
          horizontalLineTo(16f)
          verticalLineTo(19f)
          horizontalLineTo(20f)
          curveTo(21.1f, 19f, 22f, 18.1f, 22f, 17f)
          verticalLineTo(5f)
          curveTo(22f, 3.89f, 21.1f, 3f, 20f, 3f)
          close()
          moveTo(20f, 17f)
          horizontalLineTo(4f)
          verticalLineTo(5f)
          horizontalLineTo(20f)
          verticalLineTo(17f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(6f, 8.25f)
          horizontalLineTo(14f)
          verticalLineTo(9.75f)
          horizontalLineTo(6f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(16.5f, 9.75f)
          horizontalLineTo(18f)
          verticalLineTo(8.25f)
          horizontalLineTo(16.5f)
          verticalLineTo(7f)
          horizontalLineTo(15f)
          verticalLineTo(11f)
          horizontalLineTo(16.5f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(10f, 12.25f)
          horizontalLineTo(18f)
          verticalLineTo(13.75f)
          horizontalLineTo(10f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(7.5f, 15f)
          horizontalLineTo(9f)
          verticalLineTo(11f)
          horizontalLineTo(7.5f)
          verticalLineTo(12.25f)
          horizontalLineTo(6f)
          verticalLineTo(13.75f)
          horizontalLineTo(7.5f)
          close()
        }
      }.build()
    }

  val RestartService: ImageVector
    get() {
      return ImageVector.Builder(
        name = "RestartService",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(12f, 5f)
          verticalLineTo(2f)
          lineTo(8f, 6f)
          lineTo(12f, 10f)
          verticalLineTo(7f)
          curveTo(15.31f, 7f, 18f, 9.69f, 18f, 13f)
          curveTo(18f, 15.97f, 15.83f, 18.43f, 13f, 18.91f)
          verticalLineTo(20.93f)
          curveTo(16.95f, 20.44f, 20f, 17.08f, 20f, 13f)
          curveTo(20f, 8.58f, 16.42f, 5f, 12f, 5f)
          close()
        }
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(6f, 13f)
          curveTo(6f, 11.35f, 6.67f, 9.85f, 7.76f, 8.76f)
          lineTo(6.34f, 7.34f)
          curveTo(4.9f, 8.79f, 4f, 10.79f, 4f, 13f)
          curveTo(4f, 17.08f, 7.05f, 20.44f, 11f, 20.93f)
          verticalLineTo(18.91f)
          curveTo(8.17f, 18.43f, 6f, 15.97f, 6f, 13f)
          close()
        }
      }.build()
    }

  val StartService: ImageVector
    get() {
      return ImageVector.Builder(
        name = "StartService",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(20f, 18f)
          curveTo(21.1f, 18f, 21.99f, 17.1f, 21.99f, 16f)
          lineTo(22f, 6f)
          curveTo(22f, 4.89f, 21.1f, 4f, 20f, 4f)
          horizontalLineTo(4f)
          curveTo(2.89f, 4f, 2f, 4.89f, 2f, 6f)
          verticalLineTo(16f)
          curveTo(2f, 17.1f, 2.89f, 18f, 4f, 18f)
          horizontalLineTo(0f)
          verticalLineTo(20f)
          horizontalLineTo(24f)
          verticalLineTo(18f)
          horizontalLineTo(20f)
          close()
          moveTo(13f, 14.47f)
          verticalLineTo(12.28f)
          curveTo(10.22f, 12.28f, 8.39f, 13.13f, 7f, 15f)
          curveTo(7.56f, 12.33f, 9.11f, 9.67f, 13f, 9.13f)
          verticalLineTo(7f)
          lineTo(17f, 10.73f)
          lineTo(13f, 14.47f)
          close()
        }
      }.build()
    }

  val StatusStandby: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Status1Standby",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(12f, 10f)
          curveTo(10.9f, 10f, 10f, 10.9f, 10f, 12f)
          curveTo(10f, 13.1f, 10.9f, 14f, 12f, 14f)
          curveTo(13.1f, 14f, 14f, 13.1f, 14f, 12f)
          curveTo(14f, 10.9f, 13.1f, 10f, 12f, 10f)
          close()

          moveTo(12f, 12f)
          curveTo(12f, 12f, 12f, 12f, 12f, 12f)
          curveTo(12f, 12f, 12f, 12f, 12f, 12f)
          close()
        }
      }.build()
    }

  /**
   * Status1Disconnected - Disconnected or error status icon
   */
  val Status1Disconnected: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Status1Error",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        // Outer circle
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 2.0f,
          strokeLineCap = StrokeCap.Round,
          strokeLineJoin = StrokeJoin.Round,
          stroke = SolidColor(Color.Black),
          strokeLineMiter = 4.0f,
          fillAlpha = 0.0f
        ) {
          // Outer circle
          moveTo(12f, 12f)
          arcTo(10f, 10f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 2f)
          arcTo(10f, 10f, 0f, isMoreThanHalf = true, isPositiveArc = true, 22f, 12f)
          arcTo(10f, 10f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 22f)
          arcTo(10f, 10f, 0f, isMoreThanHalf = true, isPositiveArc = true, 2f, 12f)
          close()
        }

        // X mark for error
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 2.0f,
          strokeLineCap = StrokeCap.Round,
          strokeLineJoin = StrokeJoin.Round,
          stroke = SolidColor(Color.Black),
          strokeLineMiter = 4.0f,
          fillAlpha = 0.0f
        ) {
          // First line of X
          moveTo(8f, 8f)
          lineTo(16f, 16f)
        }

        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 2.0f,
          strokeLineCap = StrokeCap.Round,
          strokeLineJoin = StrokeJoin.Round,
          stroke = SolidColor(Color.Black),
          strokeLineMiter = 4.0f,
          fillAlpha = 0.0f
        ) {
          // Second line of X
          moveTo(16f, 8f)
          lineTo(8f, 16f)
        }
      }.build()
    }

  /**
   * StatusUnknown - Unknown status icon with question mark
   */
  val StatusUnknown: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Status1Unknown",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        // Outer circle
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 2.0f,
          strokeLineCap = StrokeCap.Round,
          strokeLineJoin = StrokeJoin.Round,
          stroke = SolidColor(Color.Black),
          strokeLineMiter = 4.0f,
          fillAlpha = 0.0f
        ) {
          // Outer circle
          moveTo(12f, 12f)
          arcTo(10f, 10f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 2f)
          arcTo(10f, 10f, 0f, isMoreThanHalf = true, isPositiveArc = true, 22f, 12f)
          arcTo(10f, 10f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 22f)
          arcTo(10f, 10f, 0f, isMoreThanHalf = true, isPositiveArc = true, 2f, 12f)
          close()
        }

        // Question mark
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 2.0f,
          strokeLineCap = StrokeCap.Round,
          strokeLineJoin = StrokeJoin.Round,
          stroke = SolidColor(Color.Black),
          strokeLineMiter = 4.0f,
          fillAlpha = 0.0f
        ) {
          // Question mark hook
          moveTo(12f, 8f)
          curveTo(10.9f, 8f, 10f, 8.9f, 10f, 10f)
          curveTo(10f, 11.1f, 10.4f, 12f, 11f, 12.5f)
          curveTo(11.6f, 13f, 12f, 13.5f, 12f, 14f)
          lineTo(12f, 16f)
        }

        // Question mark dot
        path(
          fill = SolidColor(Color.Black),
          pathFillType = PathFillType.NonZero,
          strokeLineWidth = 0.0f,
          strokeLineCap = StrokeCap.Butt,
          strokeLineJoin = StrokeJoin.Miter,
          strokeLineMiter = 4.0f
        ) {
          // Bottom dot
          moveTo(12f, 18f)
          arcTo(1f, 1f, 0f, isMoreThanHalf = true, isPositiveArc = true, 13f, 18f)
          arcTo(1f, 1f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 17f)
          arcTo(1f, 1f, 0f, isMoreThanHalf = true, isPositiveArc = true, 11f, 18f)
          arcTo(1f, 1f, 0f, isMoreThanHalf = true, isPositiveArc = true, 12f, 19f)
          close()
        }
      }.build()
    }

  val Status2: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Status2",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(12f, 8f)
          curveTo(9.79f, 8f, 8f, 9.79f, 8f, 12f)
          curveTo(8f, 14.21f, 9.79f, 16f, 12f, 16f)
          curveTo(14.21f, 16f, 16f, 14.21f, 16f, 12f)
          curveTo(16f, 9.79f, 14.21f, 8f, 12f, 8f)
          close()

          moveTo(12f, 14f)
          curveTo(10.9f, 14f, 10f, 13.1f, 10f, 12f)
          curveTo(10f, 10.9f, 10.9f, 10f, 12f, 10f)
          curveTo(13.1f, 10f, 14f, 10.9f, 14f, 12f)
          curveTo(14f, 13.1f, 13.1f, 14f, 12f, 14f)
          close()
        }
      }.build()
    }

  val Status3: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Status3",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(12f, 6f)
          curveTo(8.69f, 6f, 6f, 8.69f, 6f, 12f)
          curveTo(6f, 15.31f, 8.69f, 18f, 12f, 18f)
          curveTo(15.31f, 18f, 18f, 15.31f, 18f, 12f)
          curveTo(18f, 8.69f, 15.31f, 6f, 12f, 6f)
          close()

          moveTo(12f, 16f)
          curveTo(9.79f, 16f, 8f, 14.21f, 8f, 12f)
          curveTo(8f, 9.79f, 9.79f, 8f, 12f, 8f)
          curveTo(14.21f, 8f, 16f, 9.79f, 16f, 12f)
          curveTo(16f, 14.21f, 14.21f, 16f, 12f, 16f)
          close()
        }
      }.build()
    }

  val Status4: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Status4",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(12f, 4f)
          curveTo(7.59f, 4f, 4f, 7.59f, 4f, 12f)
          curveTo(4f, 16.41f, 7.59f, 20f, 12f, 20f)
          curveTo(16.41f, 20f, 20f, 16.41f, 20f, 12f)
          curveTo(20f, 7.59f, 16.41f, 4f, 12f, 4f)
          close()

          moveTo(12f, 18f)
          curveTo(8.69f, 18f, 6f, 15.31f, 6f, 12f)
          curveTo(6f, 8.69f, 8.69f, 6f, 12f, 6f)
          curveTo(15.31f, 6f, 18f, 8.69f, 18f, 12f)
          curveTo(18f, 15.31f, 15.31f, 18f, 12f, 18f)
          close()
        }
      }.build()
    }

  val Status5: ImageVector
    get() {
      return ImageVector.Builder(
        name = "Status5",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(12f, 2f)
          curveTo(6.47f, 2f, 2f, 6.47f, 2f, 12f)
          curveTo(2f, 17.53f, 6.47f, 22f, 12f, 22f)
          curveTo(17.53f, 22f, 22f, 17.53f, 22f, 12f)
          curveTo(22f, 6.47f, 17.53f, 2f, 12f, 2f)
          close()

          moveTo(12f, 20f)
          curveTo(7.58f, 20f, 4f, 16.42f, 4f, 12f)
          curveTo(4f, 7.58f, 7.58f, 4f, 12f, 4f)
          curveTo(16.42f, 4f, 20f, 7.58f, 20f, 12f)
          curveTo(20f, 16.42f, 16.42f, 20f, 12f, 20f)
          close()
        }
      }.build()
    }

  val StopService: ImageVector
    get() {
      return ImageVector.Builder(
        name = "StopService",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(21.22f, 18.02f)
          lineTo(23.22f, 20.02f)
          lineTo(24f, 20.02f)
          verticalLineTo(18.02f)
          horizontalLineTo(21.22f)
          close()

          moveTo(21.99f, 16.02f)
          lineTo(22f, 6.02f)
          curveTo(22f, 4.91f, 21.1f, 4.02f, 20f, 4.02f)
          lineTo(7.22f, 4.02f)
          lineTo(12.45f, 9.25f)
          curveTo(12.63f, 9.21f, 12.81f, 9.18f, 13f, 9.15f)
          lineTo(13f, 7.02f)
          lineTo(17f, 10.75f)
          lineTo(15.42f, 12.22f)
          lineTo(20.96f, 17.76f)
          curveTo(21.57f, 17.43f, 21.99f, 16.77f, 21.99f, 16.02f)
          close()

          moveTo(2.39f, 1.73f)
          lineTo(1.11f, 3f)
          lineTo(2.65f, 4.54f)
          curveTo(2.25f, 4.9f, 2f, 5.43f, 2f, 6.02f)
          verticalLineTo(16.02f)
          curveTo(2f, 17.12f, 2.89f, 18.02f, 4f, 18.02f)
          horizontalLineTo(0f)
          verticalLineTo(20.02f)
          horizontalLineTo(18.13f)
          lineTo(20.84f, 22.73f)
          lineTo(22.11f, 21.46f)
          lineTo(2.39f, 1.73f)
          close()

          moveTo(7f, 15.02f)
          curveTo(7.31f, 13.54f, 7.92f, 12.07f, 9.07f, 10.96f)
          lineTo(10.66f, 12.55f)
          curveTo(9.12f, 12.93f, 7.96f, 13.73f, 7f, 15.02f)
          close()
        }
      }.build()
    }

  val SurfaceRotate: ImageVector
    get() {
      return ImageVector.Builder(
        name = "SurfaceRotate",
        defaultWidth = 24.dp,
        defaultHeight = 24.dp,
        viewportWidth = 24f,
        viewportHeight = 24f
      ).apply {
        path(
          fill = SolidColor(Color.White),
          pathFillType = PathFillType.NonZero
        ) {
          moveTo(4f, 7.59f)
          lineTo(9f, 2.59f)
          curveTo(9.78f, 1.81f, 11.05f, 1.81f, 11.83f, 2.59f)
          lineTo(20.24f, 11f)
          horizontalLineTo(17.41f)
          lineTo(10.4f, 4f)
          lineTo(5.41f, 9f)
          horizontalLineTo(8f)
          verticalLineTo(11f)
          horizontalLineTo(2f)
          verticalLineTo(5f)
          horizontalLineTo(4f)
          verticalLineTo(7.59f)
          close()

          moveTo(20f, 19f)
          horizontalLineTo(22f)
          verticalLineTo(13f)
          horizontalLineTo(16f)
          verticalLineTo(15f)
          horizontalLineTo(18.59f)
          lineTo(13.6f, 20f)
          lineTo(6.59f, 13f)
          horizontalLineTo(3.76f)
          lineTo(12.17f, 21.41f)
          curveTo(12.95f, 22.19f, 14.22f, 22.19f, 15f, 21.41f)
          lineTo(20f, 16.41f)
          verticalLineTo(19f)
          close()
        }
      }.build()
    }

}