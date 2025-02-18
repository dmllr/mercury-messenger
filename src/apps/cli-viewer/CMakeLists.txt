add_executable(cli-viewer
  viewer.cpp
)
set_property(TARGET cli-viewer PROPERTY CXX_STANDARD 20)
target_compile_features(cli-viewer PRIVATE cxx_std_20)
target_link_libraries(cli-viewer PRIVATE
  hela-eillance-core
)
