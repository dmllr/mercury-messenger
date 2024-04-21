set(OpenCV_VERSION 4.9.0)

set(BUILD_SHARED_LIBS OFF)

set(BUILD_ZLIB OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOCS OFF)
set(BUILD_PACKAGE OFF)
set(BUILD_PERF_TESTS OFF)
set(BUILD_TESTS OFF)
set(BUILD_JAVA OFF)
set(BUILD_OBJC OFF)
set(BUILD_KOTLIN_EXTENSIONS OFF)
set(BUILD_opencv_apps OFF)
set(BUILD_opencv_js OFF)
set(WITH_FFMPEG ON)
set(WITH_GSTREAMER OFF)
set(OPENCV_ENABLE_NONFREE OFF)
set(OPENCV_DISABLE_FILESYSTEM_SUPPORT ON)

if (ANDROID)
    set(ANDROID_SDK_ROOT "${ANDROID_TOOLCHAIN_ROOT}")
    set(ANDROID_MIN_SDK_VERSION 18)
    set(ANDROID_TARGET_SDK_VERSION 34)
    set(BUILD_ANDROID_PROJECTS OFF)
    set(BUILD_ANDROID_EXAMPLES OFF)
    set(BUILD_ANDROID_SERVICE OFF)
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
