add_library(eillance-storage-sqlite INTERFACE)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

include("${shared_cmake_libs_path}/libsqlite.cmake")

target_include_directories(eillance-storage-sqlite INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(eillance-storage-sqlite INTERFACE
  eillance-logging
  eillance-core
  eillance-utils
  SQLite3
)

if(ANDROID)
  target_link_libraries(eillance-storage-sqlite INTERFACE log)
endif()
