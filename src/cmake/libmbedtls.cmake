set(DISABLE_PACKAGE_CONFIG_AND_INSTALL OFF)
import_remote("https://github.com/Mbed-TLS/mbedtls.git"
  TAG "v3.6.0"
  DST "${CMAKE_SOURCE_DIR}/_imported/libmbedtls"
  ALIAS libmbedtls)

target_compile_definitions(mbedtls PUBLIC MBEDTLS_SSL_DTLS_SRTP)

add_library(MbedTLS::MbedTLS ALIAS mbedtls)
add_library(MbedTLS::MbedCrypto ALIAS mbedcrypto)
add_library(MbedTLS::MbedX509 ALIAS mbedx509)

set(MBEDTLS_INCLUDE_DIRS "${libmbedtls_path}/include")
set(MBEDTLS_LIBRARIES MbedTLS::MbedTLS MbedTLS::MbedCrypto MbedTLS::MbedX509)
set(MBEDTLS_LIBRARY mbedtls)
set(MBEDX509_LIBRARY mbedx509)
set(MBEDCRYPTO_LIBRARY mbedcrypto)
