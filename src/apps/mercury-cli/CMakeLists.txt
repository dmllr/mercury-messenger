project(mercury-cli CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mercury-cli
  mercury.cpp
)

set_property(TARGET mercury-cli PROPERTY CXX_STANDARD 20)
target_compile_features(mercury-cli PRIVATE cxx_std_20)

target_link_libraries(mercury-cli PRIVATE
  hela-eillance-core
)
