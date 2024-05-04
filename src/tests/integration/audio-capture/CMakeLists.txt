set(src_path "../../..")

add_executable(audio-capture
  main.cpp
)
set_property(TARGET audio-capture PROPERTY CXX_STANDARD 17)
target_compile_features(audio-capture PRIVATE cxx_std_17)
target_link_libraries(audio-capture PRIVATE
  libminiaudio
)
