#set(BUILD_SHARED_LIBS OFF)
set(GO_EXECUTABLE "go")

if(WIN32)
  set(USE_WIN32_CRYPTO ON)
endif()

import_remote("https://boringssl.googlesource.com/boringssl" TAG "master" DST "${CMAKE_SOURCE_DIR}/_imported/libboringssl" ALIAS boringssl)
