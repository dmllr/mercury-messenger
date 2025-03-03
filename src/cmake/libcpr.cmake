# Modern C++ HTTP library
set(CURL_ZLIB Off CACHE INTERNAL "" FORCE)
set(CURL_USE_LIBPSL Off CACHE INTERNAL "" FORCE)
set(BUILD_SHARED_LIBS Off CACHE INTERNAL "" FORCE)

if (${ENABLE_MBEDTLS})
  set(SSL_BACKEND_USED "MbedTLS" CACHE INTERNAL "" FORCE)
elseif (${ENABLE_OPENSSL})
  set(SSL_BACKEND_USED "OpenSSL" CACHE INTERNAL "" FORCE)
else ()
  message(FATAL_ERROR "Either MbedTLS or OpenSSL should be supported.")
endif()

if(WIN32)
  add_definitions(-DNOMINMAX)
endif()
import_remote("https://github.com/libcpr/cpr.git" TAG "1.11.2" DST "${CMAKE_SOURCE_DIR}/_imported" ALIAS cpr)
