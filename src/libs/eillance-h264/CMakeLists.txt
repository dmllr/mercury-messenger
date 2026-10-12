project(eillance-h264 CXX)

add_library(eillance-h264 INTERFACE)

target_include_directories(eillance-h264 INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
