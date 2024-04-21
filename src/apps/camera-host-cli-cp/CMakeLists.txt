set(src_path "../..")

add_executable(camera-host-cli-cp
  main.cpp
)
set_property(TARGET camera-host-cli-cp PROPERTY CXX_STANDARD 17)
target_compile_features(camera-host-cli-cp PRIVATE cxx_std_17)
target_compile_definitions(camera-host-cli-cp PUBLIC "-DBUILD_ZLIB=OFF")
target_link_libraries(camera-host-cli-cp PRIVATE
  libopencv
  libffmpeg
  datachannel-static
  plog
  nlohmann_json
  srtp2
)
target_include_directories(camera-host-cli-cp
  PRIVATE
  "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"

  "${src_path}/lib"

  # for generated files
  "${CMAKE_BINARY_DIR}"

  # tmp
  "${libdatachannel_path}/examples/streamer"
)
