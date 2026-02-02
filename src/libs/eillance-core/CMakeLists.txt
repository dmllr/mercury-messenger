add_library(eillance-core STATIC
  eillance/client/client.cpp
)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

include("${shared_cmake_libs_path}/libplog.cmake")
include("${shared_cmake_libs_path}/libjson.cmake")

target_include_directories(eillance-core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(eillance-core PUBLIC
  eillance-utils
  eillance-logging
  eillance-crypto
  eillance-storage-sqlite
  plog::plog
  nlohmann_json::nlohmann_json
)

if(ANDROID)
  target_link_libraries(eillance-core PUBLIC log)
endif()
