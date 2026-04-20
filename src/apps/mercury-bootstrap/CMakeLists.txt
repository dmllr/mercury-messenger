project(mercury-bootstrap C CXX)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

include("${shared_cmake_libs_path}/libplog.cmake")

add_executable(mercury-bootstrap
  mercury-bootstrap.cpp
)
set_property(TARGET mercury-bootstrap PROPERTY CXX_STANDARD 20)
target_compile_features(mercury-bootstrap PRIVATE cxx_std_20)
target_include_directories(mercury-bootstrap PRIVATE
  "${REPO_ROOT}/src/libs/eillance-core"
)
target_link_libraries(mercury-bootstrap PUBLIC
  plog::plog
  crypto
  nlohmann_json
  eillance-libp2p
  eillance-utils
  eillance-crypto
)
