# Modern C++ HTTP library
set(CURL_ZLIB Off CACHE INTERNAL "" FORCE)
set(CURL_USE_LIBPSL Off)
set(BUILD_SHARED_LIBS Off)
if(WIN32)
  add_definitions(-DNOMINMAX)
endif()
import_remote("https://github.com/libcpr/cpr.git" TAG "1.11.2" DST "${CMAKE_SOURCE_DIR}/_imported" ALIAS cpr)
