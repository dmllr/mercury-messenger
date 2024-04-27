set(src_path "../../..")

add_executable(calculate-capture-fps-framework
  calculate-capture-fps-framework.cpp
)
set_property(TARGET calculate-capture-fps-framework PROPERTY CXX_STANDARD 17)
target_compile_features(calculate-capture-fps-framework PRIVATE cxx_std_17)
target_compile_definitions(calculate-capture-fps-framework PUBLIC "-DBUILD_ZLIB=OFF")
target_link_libraries(calculate-capture-fps-framework PRIVATE
  libopencv
  libffmpeg
  libminiaudio
  datachannel-static
  plog
  nlohmann_json
  srtp2
)
target_include_directories(calculate-capture-fps-framework
  PRIVATE
  "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"

  "${src_path}/lib"
)
