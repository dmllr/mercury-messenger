project(signaling-server C CXX)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

include("${shared_cmake_libs_path}/libplog.cmake")

add_executable(mercury-server
  mercury-server.cpp
)
set_property(TARGET mercury-server PROPERTY CXX_STANDARD 20)
target_compile_features(mercury-server PRIVATE cxx_std_20)
target_link_libraries(mercury-server PUBLIC
  datachannel-static
  plog
  crypto
  nlohmann_json  
  eillance-crypto
  eillance-utils
)
