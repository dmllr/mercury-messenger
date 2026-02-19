set(CMAKE_PROJECT_VERSION "v0.42.0")

# for libdatachannel
set(USE_MBEDTLS OFF CACHE INTERNAL "" FORCE)
set(USE_OPENSSL ON CACHE INTERNAL "" FORCE)
set(USE_GNUTLS OFF CACHE INTERNAL "" FORCE)
set(NO_TESTS ON CACHE INTERNAL "" FORCE)
set(NO_EXAMPLES ON CACHE INTERNAL "" FORCE)
set(WARNINGS_AS_ERRORS OFF CACHE INTERNAL "" FORCE)

# for libdatachannel -> plog, we manage plog independently
include("${CMAKE_CURRENT_LIST_DIR}/libplog.cmake")
set(USE_SYSTEM_PLOG ON CACHE INTERNAL "" FORCE)

# for libdatachannel -> json
set(JSON_BuildTests OFF CACHE INTERNAL "" FORCE)

# for libdatachannel -> usrsctp
set(ENABLE_MBEDTLS ${USE_MBEDTLS} CACHE BOOL "" FORCE)
set(ENABLE_GNUTLS ${USE_GNUTLS} CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL ${USE_OPENSSL} CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)

if (${ENABLE_MBEDTLS})
  include("${CMAKE_CURRENT_LIST_DIR}/libmbedtls.cmake")
elseif (${ENABLE_OPENSSL})
  include("${CMAKE_CURRENT_LIST_DIR}/libboringssl.cmake")
else ()
  message(FATAL_ERROR "Either MbedTLS or OpenSSL should be supported.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/libsrtp.cmake")

import_remote("https://github.com/paullouisageneau/libdatachannel.git"
  TAG "v0.23.2"
  DST "${REPO_ROOT}/_imported"
  PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/libdatachannel.patch"
  ALIAS libdatachannel
)
