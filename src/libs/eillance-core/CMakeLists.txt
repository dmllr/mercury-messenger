add_library(eillance-core INTERFACE)

set(root "../../..")
set(shared_src_path "${root}/src")
set(shared_cmake_libs_path "${shared_src_path}/cmake")

include("${shared_cmake_libs_path}/libplog.cmake")
include("${shared_cmake_libs_path}/libjson.cmake")
include("${shared_cmake_libs_path}/libsqlite.cmake")

target_include_directories(eillance-core INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(eillance-core INTERFACE
  eillance-utils
  eillance-logging
  eillance-crypto
  eillance-storage-sqlite
  plog
  nlohmann_json
  SQLite3
)

if(ANDROID)
  target_link_libraries(eillance-core INTERFACE log)
endif()
