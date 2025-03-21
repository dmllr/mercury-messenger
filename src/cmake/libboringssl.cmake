set(BUILD_SHARED_LIBS OFF)
set(GO_EXECUTABLE "go")

if(WIN32)
  set(USE_WIN32_CRYPTO ON)
endif()

import_remote("https://boringssl.googlesource.com/boringssl" TAG "master" DST "${CMAKE_SOURCE_DIR}/_imported" ALIAS boringssl)

set(BORINGSSL_ROOT_DIR  "${boringssl_path}")
set(OPENSSL_ROOT_DIR "${boringssl_path}")
set(OPENSSL_INCLUDE_DIR "${boringssl_path}/include")

target_compile_definitions(ssl PUBLIC WIN32_LEAN_AND_MEAN)
target_compile_definitions(crypto PUBLIC WIN32_LEAN_AND_MEAN)
include_directories("${boringssl_path}/include")

if(TARGET ssl AND NOT TARGET eillance-ssl)
  add_library(eillance-ssl ALIAS ssl)
endif()
if(TARGET crypto AND NOT TARGET eillance-crypto)
  add_library(eillance-crypto ALIAS crypto)
endif()

message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")
message(STATUS "Checking for OpenSSLConfig.cmake in: ${CMAKE_CURRENT_LIST_DIR}/modules")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/modules/OpenSSLConfig.cmake")
  message(STATUS "OpenSSLConfig.cmake exists!")
else()
  message(STATUS "OpenSSLConfig.cmake does not exist in expected location")
endif()
message(STATUS "MODULE_PATH right before find_package: ${CMAKE_MODULE_PATH}")
message(STATUS "PREFIX_PATH right before find_package: ${CMAKE_PREFIX_PATH}")
message(STATUS "Looking for file at: ${CMAKE_CURRENT_LIST_DIR}/modules/OpenSSLConfig.cmake")

set(OpenSSL_DIR "${CMAKE_CURRENT_LIST_DIR}/modules")
find_package(OpenSSL REQUIRED)
