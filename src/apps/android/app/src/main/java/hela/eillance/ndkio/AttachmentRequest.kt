package hela.eillance.ndkio

data class AttachmentRequest(
  val path: String,
  val type: Int,
  val thumbnail: String,
  val mimeType: String,
  val note: String = "",
  val fileName: String = ""
)
