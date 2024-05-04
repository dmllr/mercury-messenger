add_executable(encode-decode-play-audio-file
  encode-decode-play-audio-file.cpp
)
set_property(TARGET encode-decode-play-audio-file PROPERTY CXX_STANDARD 17)
target_compile_features(encode-decode-play-audio-file PRIVATE cxx_std_17)
target_link_libraries(encode-decode-play-audio-file PRIVATE
  hela-eillance-core
)
target_compile_definitions(encode-decode-play-audio-file PRIVATE USE_LIBOPUS)
