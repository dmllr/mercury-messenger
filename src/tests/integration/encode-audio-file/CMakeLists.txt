add_executable(encode-audio-file
  encode-audio-file.cpp
)
set_property(TARGET encode-audio-file PROPERTY CXX_STANDARD 17)
target_compile_features(encode-audio-file PRIVATE cxx_std_17)
target_link_libraries(encode-audio-file PRIVATE
  hela-eillance-core
)
target_compile_definitions(encode-audio-file PRIVATE USE_LIBOPUS)
