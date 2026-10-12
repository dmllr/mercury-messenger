set(MBEDTLS_VERSION "3.6.2" CACHE INTERNAL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL OFF CACHE INTERNAL "" FORCE)
import_remote("https://github.com/Mbed-TLS/mbedtls.git"
  TAG "v${MBEDTLS_VERSION}"
  DST "${REPO_ROOT}/_imported"
  ALIAS libmbedtls)

target_compile_definitions(mbedtls PUBLIC MBEDTLS_SSL_DTLS_SRTP)

add_library(MbedTLS::MbedTLS ALIAS mbedtls)
add_library(MbedTLS::MbedCrypto ALIAS mbedcrypto)
add_library(MbedTLS::MbedX509 ALIAS mbedx509)

set(MbedTLS_DIR "${libmbedtls_path}/cmake" CACHE INTERNAL "" FORCE)
set(MBEDTLS_INCLUDE_DIR "${libmbedtls_path}/include" CACHE INTERNAL "" FORCE)
set(MBEDTLS_INCLUDE_DIRS "${libmbedtls_path}/include" CACHE INTERNAL "" FORCE)
set(MBEDTLS_LIBRARIES MbedTLS::MbedTLS MbedTLS::MbedCrypto MbedTLS::MbedX509 CACHE INTERNAL "" FORCE)
set(MBEDTLS_LIBRARY mbedtls CACHE INTERNAL "" FORCE)
set(MBEDX509_LIBRARY mbedx509 CACHE INTERNAL "" FORCE)
set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE INTERNAL "" FORCE)
