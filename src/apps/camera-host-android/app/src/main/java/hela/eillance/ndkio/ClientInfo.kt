package hela.eillance.ndkio

data class Resolution(
  val width: Int,
  val height: Int
)

data class Camera(
  val id: String,
  val name: String,
  val resolutions: List<Resolution>
)

data class ClientInfo(
  val id: String,
  val name: String,
  val profile: List<Camera>
)
