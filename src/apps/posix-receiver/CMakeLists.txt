set(src_path "../..")

add_executable(posix-receiver
  main.cpp
)
set_property(TARGET posix-receiver PROPERTY CXX_STANDARD 17)
target_compile_features(posix-receiver PRIVATE cxx_std_17)
target_compile_definitions(posix-receiver PUBLIC "-DBUILD_ZLIB=OFF")
target_link_libraries(posix-receiver PRIVATE
  libopencv
  libffmpeg
  datachannel
)
target_include_directories(posix-receiver
  PRIVATE
  "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"

  # for OpenCV generated files
  "${CMAKE_BINARY_DIR}"

  # tmp
  "${libdatachannel_path}/examples/streamer"
)
