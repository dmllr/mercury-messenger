add_library(hela-eillance-core INTERFACE)
target_include_directories(hela-eillance-core INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_BINARY_DIR}"
)
target_link_libraries(hela-eillance-core INTERFACE
  libopencv
  libffmpeg
  libminiaudio
  datachannel-static
  plog
  nlohmann_json
  srtp2
)