add_executable(cli-streamer
  streamer.cpp
)
set_property(TARGET cli-streamer PROPERTY CXX_STANDARD 20)
target_compile_features(cli-streamer PRIVATE cxx_std_20)
target_link_libraries(cli-streamer PRIVATE
  hela-eillance-core
)
