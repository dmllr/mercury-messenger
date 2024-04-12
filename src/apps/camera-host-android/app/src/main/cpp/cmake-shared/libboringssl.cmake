set(BUILD_SHARED_LIBS OFF)
set(GO_EXECUTABLE "go")

if(WIN32)
  set(USE_WIN32_CRYPTO ON)
endif()

import_remote("https://boringssl.googlesource.com/boringssl" TAG "master" DST "${CMAKE_SOURCE_DIR}/_imported/libboringssl" ALIAS boringssl)

set(OPENSSL_INCLUDE_DIR "${boringssl_path}/include")
add_library(OpenSSL::SSL ALIAS ssl)
add_library(OpenSSL::Crypto ALIAS crypto)

if(WIN32)
  target_compile_definitions(ssl PUBLIC WIN32_LEAN_AND_MEAN)
  target_compile_definitions(crypto PUBLIC WIN32_LEAN_AND_MEAN)
endif()
