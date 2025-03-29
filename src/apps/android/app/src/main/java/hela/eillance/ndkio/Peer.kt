package hela.eillance.ndkio

import kotlinx.serialization.Serializable

typealias Profile = List<Camera>
typealias Resolutions = List<Resolution>

@Serializable
data class Resolution(
  val width: Int,
  val height: Int
)

@Serializable
data class Camera(
  val id: String,
  val name: String,
  val resolutions: Resolutions
)

@Serializable
data class Peer(
  val id: String,
  val name: String,
  val profile: Profile
)
