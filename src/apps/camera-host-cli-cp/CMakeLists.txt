add_executable(camera-host-cli-cp
  main.cpp
)
set_property(TARGET camera-host-cli-cp PROPERTY CXX_STANDARD 17)
target_compile_features(camera-host-cli-cp PRIVATE cxx_std_17)
target_link_libraries(camera-host-cli-cp PRIVATE
  hela-eillance-core
)
target_include_directories(camera-host-cli-cp
  PRIVATE
  "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
)
