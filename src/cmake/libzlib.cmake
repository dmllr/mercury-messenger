set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
import_remote("https://github.com/madler/zlib.git"
	TAG "v1.3.2"
	DST "${REPO_ROOT}/_imported"
	ALIAS zlib
)

if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
	add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIR "${zlib_path};${zlib_binary_path}" CACHE PATH "" FORCE)
