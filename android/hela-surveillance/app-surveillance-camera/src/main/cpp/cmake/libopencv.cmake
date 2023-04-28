option(WITH_FFMPEG "WITH_FFMPEG" ON)

set(BUILD_ZLIB OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOCS OFF)
set(BUILD_PERF_TESTS OFF)
set(BUILD_TESTS OFF)
set(WITH_FFMPEG ON)
set(OPENCV_ENABLE_NONFREE ON)

import_remote("https://github.com/opencv/opencv.git" TAG "4.6.0" DST "${CMAKE_SOURCE_DIR}/_imported/libopencv" ALIAS libopencv)
