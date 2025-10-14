project(signaling-server C CXX)

include("../cmake/libdatachannel.cmake")

add_executable(hela-eillance-signaling
  signaling_server.cpp
)
set_property(TARGET hela-eillance-signaling PROPERTY CXX_STANDARD 20)
target_compile_features(hela-eillance-signaling PRIVATE cxx_std_20)
target_link_libraries(hela-eillance-signaling PUBLIC
  datachannel-static
  plog
  ssl
  crypto
  nlohmann_json
)

add_executable(hela-eillance-signaling-tester
  signaling_server_tester.cpp
)
set_property(TARGET hela-eillance-signaling-tester PROPERTY CXX_STANDARD 20)
target_compile_features(hela-eillance-signaling-tester PRIVATE cxx_std_20)
target_link_libraries(hela-eillance-signaling-tester PUBLIC
  datachannel-static
  plog
)
