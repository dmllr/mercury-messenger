set(CMAKE_PROJECT_VERSION "v0.42.0")

# for libdatachannel
set(USE_MBEDTLS ON)
set(USE_GNUTLS OFF)
set(NO_TESTS ON)
set(NO_EXAMPLES ON)
set(WARNINGS_AS_ERRORS OFF)

# for libdatachannel -> usrsctp
set(ENABLE_MBEDTLS ON)
set(ENABLE_OPENSSL OFF)
set(ENABLE_TESTING OFF)
set(ENABLE_PROGRAMS OFF)

include("src/cmake/libmbedtls.cmake")
include("src/cmake/libsrtp.cmake")

import_remote("https://github.com/paullouisageneau/libdatachannel.git"
  TAG "v0.20.2"
  DST "${CMAKE_SOURCE_DIR}/_imported/libdatachannel"
  PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/libdatachannel.patch"
  ALIAS libdatachannel
)
