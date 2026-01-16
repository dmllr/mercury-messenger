if(TARGET libopencv)
  return()
endif()

set(OpenCV_VERSION 4.11.0)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

set(OPENCV_SKIP_INSTALL_RULES ON CACHE BOOL "Completely disable all install rules" FORCE)
set(INSTALL_CREATE_DISTRIB OFF CACHE BOOL "Disable creation of distribution packages" FORCE)
set(BUILD_PACKAGE OFF CACHE BOOL "Disable building OpenCV package" FORCE)
set(OPENCV_GENERATE_PKGCONFIG OFF CACHE BOOL "Disable generating .pc files for pkg-config" FORCE)

set(SKIP_INSTALL_FILES ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

set(BUILD_EXAMPLES OFF CACHE BOOL "Do not build examples" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "Do not build documentation" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "Do not build performance tests" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Do not build tests" FORCE)
set(BUILD_JAVA OFF CACHE BOOL "Do not build Java bindings" FORCE)
set(BUILD_OBJC OFF CACHE BOOL "Do not build Objective-C bindings" FORCE)
set(BUILD_KOTLIN_EXTENSIONS OFF CACHE BOOL "Do not build Kotlin extensions" FORCE)
set(BUILD_opencv_apps OFF CACHE BOOL "Do not build OpenCV apps" FORCE)
set(BUILD_opencv_js OFF CACHE BOOL "Do not build Javascript bindings" FORCE)

# Tell OpenCV to find and use the dependencies we provide.
set(BUILD_ZLIB OFF CACHE BOOL "Do not build the bundled zlib" FORCE)
set(WITH_PROTOBUF OFF CACHE BOOL "Use external Protobuf" FORCE)
set(BUILD_PROTOBUF OFF CACHE BOOL "Do not build the bundled Protobuf" FORCE)
set(WITH_FFMPEG ON CACHE BOOL "Enable FFMPEG support" FORCE)
set(WITH_GSTREAMER OFF CACHE BOOL "Disable GStreamer support" FORCE)
set(OPENCV_ENABLE_NONFREE OFF CACHE BOOL "Disable non-free modules" FORCE)

# Unused image formats
set(WITH_PNG OFF CACHE BOOL "" FORCE)
set(WITH_TIFF OFF CACHE BOOL "" FORCE)
set(WITH_WEBP OFF CACHE BOOL "" FORCE)
set(WITH_OPENJPEG OFF CACHE BOOL "" FORCE)
set(WITH_JASPER OFF CACHE BOOL "" FORCE)
set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)

if (ANDROID)
  set(ANDROID_SDK_ROOT "${ANDROID_TOOLCHAIN_ROOT}" CACHE PATH "" FORCE)
  set(ANDROID_MIN_SDK_VERSION 18 CACHE STRING "" FORCE)
  set(ANDROID_TARGET_SDK_VERSION 34 CACHE STRING "" FORCE)
  set(BUILD_ANDROID_PROJECTS OFF CACHE BOOL "Do not build Android projects" FORCE)
  set(BUILD_ANDROID_EXAMPLES OFF CACHE BOOL "Do not build Android examples" FORCE)
  set(BUILD_ANDROID_SERVICE OFF CACHE BOOL "Do not build Android service" FORCE)
endif()

import_remote(
  "https://github.com/opencv/opencv.git"
  TAG "${OpenCV_VERSION}"
  DST "${CMAKE_SOURCE_DIR}/_imported"
  PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/libopencv.patch"
  ALIAS libopencv
)

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
  "${libopencv_path}/3rdparty/zlib"
  "${CMAKE_BINARY_DIR}"
)
