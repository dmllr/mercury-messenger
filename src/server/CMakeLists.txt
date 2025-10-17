project(signaling-server C CXX)

include("../cmake/libdatachannel.cmake")

add_executable(mercury-server
  mercury-server.cpp
)
set_property(TARGET mercury-server PROPERTY CXX_STANDARD 20)
target_compile_features(mercury-server PRIVATE cxx_std_20)
target_link_libraries(mercury-server PUBLIC
  datachannel-static
  plog
  ssl
  crypto
  nlohmann_json
)
