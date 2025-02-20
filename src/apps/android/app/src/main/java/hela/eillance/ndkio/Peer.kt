package hela.eillance.ndkio

typealias Devices = List<Peer>
typealias Profile = List<Camera>
typealias Resolutions = List<Resolution>

data class Resolution(
  val width: Int,
  val height: Int
)

data class Camera(
  val id: String,
  val name: String,
  val resolutions: Resolutions
)

data class Peer(
  val id: String,
  val name: String,
  val profile: Profile
)
