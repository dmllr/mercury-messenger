set(src_path "../..")

add_executable(audio-recorder
  main.cpp
)
set_property(TARGET audio-recorder PROPERTY CXX_STANDARD 17)
target_compile_features(audio-recorder PRIVATE cxx_std_17)
target_link_libraries(audio-recorder PRIVATE
  libminiaudio
)
