set(OpenCV_VERSION 4.9.0)

if(AAAANDROID)
    set(OpenCV_URL https://jaist.dl.sourceforge.net/project/opencvlibrary/${OpenCV_VERSION}/opencv-${OpenCV_VERSION}-android-sdk.zip)
    get_filename_component(OpenCV_ARCHIVE_NAME ${OpenCV_URL} NAME)
    set(OpenCV_PATH "${CMAKE_SOURCE_DIR}/_imported/OpenCV-android-sdk")
    set(OpenCV_ARCHIVE_PATH "${CMAKE_SOURCE_DIR}/_imported/${OpenCV_ARCHIVE_NAME}")

    IF (NOT EXISTS "${OpenCV_PATH}")
        file(DOWNLOAD ${OpenCV_URL} "${OpenCV_ARCHIVE_PATH}")

        execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${OpenCV_ARCHIVE_PATH}"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/_imported"
        )
    ENDIF()

    set(OPENCV_DISABLE_FILESYSTEM_SUPPORT ON)
    add_compile_definitions(OPENCV_DISABLE_FILESYSTEM_SUPPORT=1)
    set(OpenCV_DIR "${OpenCV_PATH}/sdk/native/jni")
    find_package(OpenCV REQUIRED)
else()
    option(WITH_FFMPEG "WITH_FFMPEG" ON)

    set(BUILD_ZLIB OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_DOCS OFF)
    set(BUILD_PERF_TESTS OFF)
    set(BUILD_TESTS OFF)
    set(WITH_FFMPEG ON)
    set(WITH_GSTREAMER OFF)
    set(OPENCV_ENABLE_NONFREE OFF)

    if (ANDROID)
        set(ANDROID_SDK_ROOT "${ANDROID_TOOLCHAIN_ROOT}")
        set(BUILD_ANDROID_PROJECTS OFF)
        set(OPENCV_DISABLE_FILESYSTEM_SUPPORT ON)
    endif()

    import_remote("https://github.com/opencv/opencv.git" TAG "${OpenCV_VERSION}" DST "${CMAKE_SOURCE_DIR}/_imported/libopencv" ALIAS libopencv)
endif()
