if(TARGET libopencv)
    return()
endif()

set(OpenCV_VERSION 4.12.0)

set(BUILD_SHARED_LIBS OFF)

# Dependencies configuration
set(BUILD_ZLIB OFF)
set(WITH_IPP OFF)
set(WITH_ITT OFF)
set(WITH_ADE OFF)

# Unused image formats
set(WITH_PNG OFF)
set(WITH_TIFF OFF)
set(WITH_WEBP OFF)
set(WITH_OPENJPEG OFF)
set(WITH_JASPER OFF)
set(WITH_OPENEXR OFF)

# General build options
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
set(BUILD_opencv_python_bindings_generator OFF)
set(BUILD_opencv_python_tests OFF)

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

import_remote(
  "https://github.com/opencv/opencv.git"
  TAG "${OpenCV_VERSION}"
  DST "${CMAKE_SOURCE_DIR}/_imported"
  ALIAS libopencv
)

add_library(libopencv INTERFACE)
target_link_libraries(libopencv INTERFACE
  opencv_core opencv_highgui opencv_imgproc opencv_videoio
)

# Note: BUILD_ZLIB=ON makes zlib headers available in the binary dir, so we include CMAKE_BINARY_DIR
target_include_directories(libopencv INTERFACE
  "${libopencv_path}/include"
  "${libopencv_path}/modules/core/include"
  "${libopencv_path}/modules/imgcodecs/include"
  "${libopencv_path}/modules/imgproc/include"
  "${libopencv_path}/modules/highgui/include"
  "${libopencv_path}/modules/videoio/include"
  "${libopencv_path}/3rdparty/zlib"
  "${CMAKE_BINARY_DIR}"
)
