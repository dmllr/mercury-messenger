project(eillance-storage-sqlite CXX)

add_library(eillance-storage-sqlite INTERFACE)

target_include_directories(eillance-storage-sqlite INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
