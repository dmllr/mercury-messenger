set(OpenCV_VERSION 4.9.0)

set(BUILD_ZLIB OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOCS OFF)
set(BUILD_PERF_TESTS OFF)
set(BUILD_TESTS OFF)
set(WITH_FFMPEG OFF)
set(WITH_GSTREAMER OFF)
set(OPENCV_ENABLE_NONFREE OFF)
set(OPENCV_DISABLE_FILESYSTEM_SUPPORT ON)

if (ANDROID)
    set(ANDROID_SDK_ROOT "${ANDROID_TOOLCHAIN_ROOT}")
    set(BUILD_ANDROID_PROJECTS OFF)
endif()

import_remote("https://github.com/opencv/opencv.git" TAG "${OpenCV_VERSION}" DST "${CMAKE_SOURCE_DIR}/_imported/libopencv" ALIAS libopencv)

add_library(libopencv INTERFACE)
target_link_libraries(libopencv INTERFACE
  opencv_core opencv_highgui opencv_imgproc opencv_videoio
)
target_include_directories(libopencv INTERFACE
  "${libopencv_path}/include"
  "${libopencv_path}/modules/core/include"
  "${libopencv_path}/modules/imgcodecs/include"
  "${libopencv_path}/modules/imgproc/include"
  "${libopencv_path}/modules/highgui/include"
  "${libopencv_path}/modules/videoio/include"
)
