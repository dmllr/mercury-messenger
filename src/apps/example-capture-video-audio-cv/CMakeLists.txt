set(src_path "../..")

add_executable(example-capture-video-audio-cv
  main.cpp
)
set_property(TARGET example-capture-video-audio-cv PROPERTY CXX_STANDARD 17)
target_compile_features(example-capture-video-audio-cv PRIVATE cxx_std_17)
target_compile_definitions(example-capture-video-audio-cv PUBLIC "-DBUILD_ZLIB=OFF")
target_link_libraries(example-capture-video-audio-cv PRIVATE
  libopencv
  libffmpeg
  libminiaudio
  datachannel-static
  plog
  nlohmann_json
  srtp2
)
target_include_directories(example-capture-video-audio-cv
  PRIVATE
  "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"

  "${src_path}/lib"
)
