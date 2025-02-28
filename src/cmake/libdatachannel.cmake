set(CMAKE_PROJECT_VERSION "v0.42.0")

# for libdatachannel
set(USE_MBEDTLS OFF)
set(USE_GNUTLS OFF)
set(NO_TESTS ON)
set(NO_EXAMPLES ON)
set(WARNINGS_AS_ERRORS OFF)

# for libdatachannel -> plog
set(PLOG_BUILD_TESTS OFF)
set(PLOG_BUILD_SAMPLES OFF)
set(PLOG_INSTALL OFF)

# for libdatachannel -> json
set(JSON_BuildTests OFF)

# for libdatachannel -> usrsctp
set(ENABLE_MBEDTLS ${USE_MBEDTLS})
set(ENABLE_GNUTLS ${USE_GNUTLS})
if (NOT ${USE_MBEDTLS} AND NOT ${USE_GNUTLS})
  set(ENABLE_OPENSSL ON)
else()
  set(ENABLE_OPENSSL OFF)
endif()
set(ENABLE_TESTING OFF)
set(ENABLE_PROGRAMS OFF)

if (${ENABLE_MBEDTLS})
  include("${CMAKE_CURRENT_LIST_DIR}/libmbedtls.cmake")
endif()
if (${ENABLE_GNUTLS})
  message(FATAL_ERROR "GnuTLS is not supported")
endif()
if (${ENABLE_OPENSSL})
  include("${CMAKE_CURRENT_LIST_DIR}/libboringssl.cmake")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/libsrtp.cmake")

import_remote("https://github.com/paullouisageneau/libdatachannel.git"
  TAG "v0.22.4"
  DST "${CMAKE_SOURCE_DIR}/_imported"
  PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/libdatachannel.patch"
  ALIAS libdatachannel
)
