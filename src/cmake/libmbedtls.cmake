#set(DISABLE_PACKAGE_CONFIG_AND_INSTALL OFF)
#set(CMAKE_INSTALL_LIBDIR "${CMAKE_SOURCE_DIR}/_imported/libmbedtls_install")

#import_remote("https://github.com/Mbed-TLS/mbedtls.git" TAG "v3.6.0" DST "${CMAKE_SOURCE_DIR}/_imported/libmbedtls" ALIAS libmbedtls)
#set(MbedTLS_DIR "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/_imported.build/https-/github-com/Mbed-TLS/mbedtls-git/cmake")
#set(MbedTLS_DIR "C:/Users/DenisMiller(WovenbyT/dev/hela-surveillance/out/build/x64-Debug/CMakeFiles/_imported.build/https-/github-com/Mbed-TLS/mbedtls-git/cmake")

#list(APPEND CMAKE_PREFIX_PATH "${MbedTLS_DIR}")
#list(APPEND CMAKE_MODULE_PATH "${MbedTLS_DIR}")

include(ExternalProject)

set(MBEDTLS_INSTALL_DIR "${CMAKE_SOURCE_DIR}/_imported/libmbedtls_install")

ExternalProject_Add(
    libmbedtls
    GIT_REPOSITORY "https://github.com/Mbed-TLS/mbedtls.git"
    GIT_TAG "v3.6.0"
    SOURCE_DIR "${MBEDTLS_INSTALL_DIR}"
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX="${MBEDTLS_INSTALL_DIR}" -DLIB_INSTALL_DIR="${MBEDTLS_INSTALL_DIR}"
    BUILD_BYPRODUCTS
        "${MBEDTLS_INSTALL_DIR}/lib/cmake/MbedTLS/MbedTLSConfig.cmake"
        "${MBEDTLS_INSTALL_DIR}/lib/cmake/MbedTLS/MbedTLSTargets.cmake"
    STEP_TARGETS build
    EXCLUDE_FROM_ALL TRUE
)

set(MbedTLS_DIR "${MBEDTLS_INSTALL_DIR}/lib/cmake/MbedTLS")
list(APPEND CMAKE_MODULE_PATH "${MBEDTLS_INSTALL_DIR}/lib/cmake/MbedTLS")


#set(MbedTLS_DIR "C:/Users/DenisMiller(WovenbyT/dev/hela-surveillance/_imported/libmbedtls/out/install/x64-Debug/lib/cmake/MbedTLS")
list(APPEND CMAKE_PREFIX_PATH "C:/Users/DenisMiller(WovenbyT/dev/hela-surveillance/_imported/libmbedtls/out/install/x64-Debug")
#list(APPEND CMAKE_PREFIX_PATH "C:/Users/DenisMiller(WovenbyT/dev/hela-surveillance/_imported/libmbedtls/out/install/x64-Debug/lib/cmake/MbedTLS")
#list(APPEND CMAKE_MODULE_PATH "C:/Users/DenisMiller(WovenbyT/dev/hela-surveillance/_imported/libmbedtls/out/install/x64-Debug/lib/cmake/MbedTLS")
