add_executable(calculate-capture-fps-pure-ocv
  calculate-capture-fps-pure-ocv.cpp
)
set_property(TARGET calculate-capture-fps-pure-ocv PROPERTY CXX_STANDARD 17)
target_compile_features(calculate-capture-fps-pure-ocv PRIVATE cxx_std_17)
target_compile_definitions(calculate-capture-fps-pure-ocv PUBLIC "-DBUILD_ZLIB=OFF")
target_link_libraries(calculate-capture-fps-pure-ocv PRIVATE libopencv)
