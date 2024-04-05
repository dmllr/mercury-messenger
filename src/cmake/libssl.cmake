#if(ANDROID)
#	set(CROSS_ANDROID ON)
#endif()
#set(BUILD_OPENSSL ON)
#set(SYSTEM_OPENSSL OFF)
#set(OPENSSL_ENABLE_TESTS OFF)
#set(OPENSSL_BUILD_VERSION 1.1.1t)
#
#set(OPENSSL_MODULES "no-cast no-shared no-md2 no-md4 no-mdc2 no-rc4 no-rc5 no-hw no-engine no-idea no-mdc2 no-rc5 no-camellia no-heartbeats no-gost no-deprecated no-capieng no-comp no-psk no-dso no-dsa no-rc2 no-des" CACHE STRING "" FORCE)
#
#import_remote("https://github.com/viaduck/openssl-cmake.git" TAG "master" DST "${CMAKE_SOURCE_DIR}/_imported/libopenssl" ALIAS openssl)





#set(BUILD_SHARED_LIBS OFF)
#set(GO_EXECUTABLE "go")
#
#if(WIN32)
#  set(USE_WIN32_CRYPTO ON)
#endif()
#
#import_remote("https://boringssl.googlesource.com/boringssl" TAG "master" DST "${CMAKE_SOURCE_DIR}/_imported/libboringssl" ALIAS boringssl)

set(LIB_INSTALL_DIR "${CMAKE_SOURCE_DIR}/_imported/libmbedtls_install")
import_remote("https://github.com/Mbed-TLS/mbedtls.git" TAG "v3.6.0" DST "${CMAKE_SOURCE_DIR}/_imported/libmbedtls" ALIAS libmbedtls)
