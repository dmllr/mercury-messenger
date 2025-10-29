project(signaling-server C CXX)

set(root "../..")
set(shared_src_path "${root}/src")
set(shared_cmake_libs_path "${shared_src_path}/cmake")

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
