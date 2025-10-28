add_executable(encode-decode-play
  encode-decode-play.cpp
)
set_property(TARGET encode-decode-play PROPERTY CXX_STANDARD 17)
target_compile_features(encode-decode-play PRIVATE cxx_std_17)
target_link_libraries(encode-decode-play PRIVATE
  eillance-core
)
target_compile_definitions(encode-decode-play PRIVATE USE_LIBOPUS)
