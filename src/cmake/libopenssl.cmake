set(BUILD_OPENSSL ON)
set(SYSTEM_OPENSSL OFF)
set(OPENSSL_ENABLE_TESTS OFF)
set(OPENSSL_BUILD_VERSION 1.1.1t)

if (ANDROID)
	set(CROSS_ANDROID ON)
endif()

set(OPENSSL_MODULES "no-cast no-shared no-md2 no-md4 no-mdc2 no-rc4 no-rc5 no-hw no-engine no-idea no-mdc2 no-rc5 no-camellia no-heartbeats no-gost no-deprecated no-capieng no-comp no-psk no-dso no-dsa no-rc2 no-des" CACHE STRING "" FORCE)

import_remote("https://github.com/viaduck/openssl-cmake.git" TAG "v3" DST "${CMAKE_SOURCE_DIR}/_imported/libopenssl" ALIAS openssl)
