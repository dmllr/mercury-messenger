set(src_path "../..")

add_executable(posix-server
  main.cpp
)
set_property(TARGET posix-server PROPERTY CXX_STANDARD 17)
target_compile_features(posix-server PRIVATE cxx_std_17)
target_compile_definitions(posix-server PUBLIC "-DBUILD_ZLIB=OFF")
target_link_libraries(posix-server PRIVATE
  opencv_core opencv_highgui opencv_imgproc opencv_videoio
  libffmpeg
  datachannel-static
  plog
  nlohmann_json
  srtp2
)
target_include_directories(posix-server
  PRIVATE
  "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"

  "${src_path}/lib"
  "${libopencv_path}/include"
  "${libopencv_path}/modules/core/include"
  "${libopencv_path}/modules/imgcodecs/include"
  "${libopencv_path}/modules/imgproc/include"
  "${libopencv_path}/modules/highgui/include"
  "${libopencv_path}/modules/videoio/include"

  # for generated files
  "${CMAKE_BINARY_DIR}"

  # tmp
  "${libdatachannel_path}/examples/streamer"
)
add_dependencies(posix-server generate_enums)
