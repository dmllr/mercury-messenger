set(shared_libs "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../lib")

add_library(camera-lib SHARED
  lib.cpp
  audio/recorder.h
  audio/recorder.cpp
  audio/audio_common.h
  audio/audio_common.cpp

  ${shared_libs}/rtc/dispatch_queue.cpp
  ${shared_libs}/rtc/stream.cpp
  ${shared_libs}/rtc/rtc_transport_base.cpp
)

target_compile_features(camera-lib PUBLIC cxx_std_20)

target_link_libraries(camera-lib
  android
  log
  camera2ndk
  mediandk
  aaudio
  liveMedia
  ssl
  crypto
  datachannel
  plog
  nlohmann_json
  opus
)

target_include_directories(camera-lib
  PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}"
  "${shared_libs}"
)

# workaround for "ninja: error: build.ninja multiple rules generate tests"
# remove it, build, undo removal, build again
add_test(tests
  tests.cpp
)
