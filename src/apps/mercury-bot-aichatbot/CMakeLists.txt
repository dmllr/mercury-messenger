project(mercury-bot-aichatbot CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

include("${shared_cmake_libs_path}/setup.cmake")
include("${shared_cmake_libs_path}/import_remote.cmake")
include("${shared_cmake_libs_path}/libboringssl.cmake")
include("${shared_cmake_libs_path}/libzlib.cmake")
include("${shared_cmake_libs_path}/libcurl.cmake")

add_executable(mercury-bot-aichatbot
  mercury-bot-aichatbot.cpp
)

set_property(TARGET mercury-bot-aichatbot PROPERTY CXX_STANDARD 20)
target_compile_features(mercury-bot-aichatbot PRIVATE cxx_std_20)

target_link_libraries(mercury-bot-aichatbot PRIVATE
  eillance-bot-api
  eillance-logging
  libcurl_static
)
target_include_directories(mercury-bot-aichatbot PUBLIC "${OPENSSL_INCLUDE_DIR}")
