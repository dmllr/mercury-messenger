package hela.eillance.ndkio

data class VideoConfig(
  var cameraId: Int = 0,
  var width: Int = 0,
  var height: Int = 0,
  var frameRate: Int = 0,
  var bitrate: Int = 0,
)

data class AudioConfig(
  var channels: Int = 2,
)

data class Config(
  var video: VideoConfig = VideoConfig(),
  var audio: AudioConfig = AudioConfig(),
)
