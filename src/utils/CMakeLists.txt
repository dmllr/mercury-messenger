project(eillance-utils CXX)

add_library(eillance-utils INTERFACE)

target_include_directories(eillance-utils INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
