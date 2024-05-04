add_executable(audio-recorder
  main.cpp
)
set_property(TARGET audio-recorder PROPERTY CXX_STANDARD 17)
target_compile_features(audio-recorder PRIVATE cxx_std_17)
target_link_libraries(audio-recorder PRIVATE
  libminiaudio
)

add_executable(opus-recorder
  opus.cpp
)
set_property(TARGET opus-recorder PROPERTY CXX_STANDARD 17)
target_compile_features(opus-recorder PRIVATE cxx_std_17)
target_link_libraries(opus-recorder PRIVATE
  libminiaudio
  opus
)
