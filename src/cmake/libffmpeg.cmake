# reference:
# https://github.com/IljaKosynkin/FFmpeg-Development-Kit/tree/master/ffmpeg-sample/app/src/main/cpp
# LIB_X264 is not included

link_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
include_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/libffmpeg.include)

include(ExternalProject)

set(FFMPEG_LIBS avutil swresample avcodec avformat swscale avfilter avdevice)
set(FFMPEG_CONFIGURE_EXTRAS --enable-jni)

include(${CMAKE_CURRENT_LIST_DIR}/libffmpeg/ffmpeg.cmake)
