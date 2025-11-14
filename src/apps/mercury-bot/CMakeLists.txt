project(mercury-bot CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(root "../../..")
set(shared_src_path "${root}/src")
set(shared_cmake_libs_path "${shared_src_path}/cmake")

include("${shared_cmake_libs_path}/setup.cmake")
include("${shared_cmake_libs_path}/import_remote.cmake")

add_executable(mercury-bot
  mercury-bot.cpp
)

set_property(TARGET mercury-bot PROPERTY CXX_STANDARD 20)
target_compile_features(mercury-bot PRIVATE cxx_std_20)

target_link_libraries(mercury-bot PRIVATE
  eillance-core
  eillance-rtc
)
