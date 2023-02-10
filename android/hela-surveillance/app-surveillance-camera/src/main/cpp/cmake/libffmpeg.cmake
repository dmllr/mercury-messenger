# sudo apt install libavcodec-dev
# sudo apt install libavcformat-dev
# sudo apt install libavformat-dev
# sudo apt install libavdevice-dev
# sudo apt install libswscale-dev


if(UNIX AND NOT APPLE)
    set(PKG_CONFIG_EXECUTABLE "pkg-config")
    set(AVCODEC_PKGCONFIG_NAME "libavcodec")
    set(AVFORMAT_PKGCONFIG_NAME "libavformat")
    set(AVUTIL_PKGCONFIG_NAME "libavutil")
    set(SWSCALE_PKGCONFIG_NAME "libswscale")
elseif(APPLE)
    set(PKG_CONFIG_EXECUTABLE "brew list pkg-config || pkg-config")
    set(AVCODEC_PKGCONFIG_NAME "ffmpeg-4.3 libavcodec")
    set(AVFORMAT_PKGCONFIG_NAME "ffmpeg-4.3 libavformat")
    set(AVUTIL_PKGCONFIG_NAME "ffmpeg-4.3 libavutil")
    set(SWSCALE_PKGCONFIG_NAME "ffmpeg-4.3 libswscale")
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(AVCODEC REQUIRED ${AVCODEC_PKGCONFIG_NAME})
pkg_check_modules(AVFORMAT REQUIRED ${AVFORMAT_PKGCONFIG_NAME})
pkg_check_modules(AVUTIL REQUIRED ${AVUTIL_PKGCONFIG_NAME})
pkg_check_modules(SWSCALE REQUIRED ${SWSCALE_PKGCONFIG_NAME})

#include_directories(${AVCODEC_INCLUDE_DIRS} ${AVFORMAT_INCLUDE_DIRS}
#        ${AVUTIL_INCLUDE_DIRS} ${SWSCALE_INCLUDE_DIRS})
#target_link_libraries(your_target ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES}
#        ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES})



#find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
#find_library(AVCODEC_LIBRARY avcodec)
#
#find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
#find_library(AVFORMAT_LIBRARY avformat)
#
#find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
#find_library(AVUTIL_LIBRARY avutil)
#
#find_path(AVDEVICE_INCLUDE_DIR libavdevice/avdevice.h)
#find_library(AVDEVICE_LIBRARY avdevice)
#
#find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h)
#find_library(SWSCALE_LIBRARY swscale)

#include_directories(${AVCODEC_INCLUDE_DIR})
#include_directories(${AVFORMAT_INCLUDE_DIR})
#include_directories(${AVUTIL_INCLUDE_DIR})
#include_directories(${AVDEVICE_INCLUDE_DIR})
#include_directories(${SWSCALE_INCLUDE_DIR})
#link_libraries(${AVCODEC_LIBRARY})
#link_libraries(${AVFORMAT_LIBRARY})
#link_libraries(${AVUTIL_LIBRARY})
#link_libraries(${AVDEVICE_LIBRARY})
#link_libraries(${SWSCALE_LIBRARY})

#find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
#find_library(AVCODEC_LIBRARY avcodec)
#
#find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
#find_library(AVFORMAT_LIBRARY avformat)
#
#find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
#find_library(AVUTIL_LIBRARY avutil)
#
#find_path(AVDEVICE_INCLUDE_DIR libavdevice/avdevice.h)
#find_library(AVDEVICE_LIBRARY avdevice)


#find_package(PkgConfig REQUIRED)
#pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
#        libavdevice
#        libavfilter
#        libavformat
#        libavcodec
#        libswresample
#        libswscale
#        libavutil
#        )
#set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})

#
#import_remote("https://github.com/FFmpeg/FFmpeg.git" TAG "v0.6.1" DST "${CMAKE_SOURCE_DIR}/_imported/libffmpeg" NO_CMAKE ALIAS libffmpeg)
#
#enable_language(ASM)
##
##find_program (YASM_EXECUTABLE yasm)
##set(CMAKE_ASM_NASM_COMPILER ${YASM_EXECUTABLE})
##set(CMAKE_ASM_NASM_COMPILER_ID YASM)
#
#configure_file("${CMAKE_CURRENT_LIST_DIR}/libffmpeg.in/config.h.in"   "${CMAKE_CURRENT_BINARY_DIR}/libffmpeg/config.h"   @ONLY)
#configure_file("${CMAKE_CURRENT_LIST_DIR}/libffmpeg.in/config.asm.in" "${CMAKE_CURRENT_BINARY_DIR}/libffmpeg/config.asm" @ONLY)
#
#add_library(avutil)
#
#file(GLOB avutil_sources CONFIGURE_DEPENDS "${libffmpeg_path}/libavutil/*.c")
#if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
#    file(GLOB avutil_sources_x86 CONFIGURE_DEPENDS "${libffmpeg_path}/libavutil/x86/*.c")
#    list(APPEND avutil_sources ${avutil_sources_x86})
#endif()
#target_sources(avutil PRIVATE ${avutil_sources})
#target_include_directories(avutil
#    PRIVATE
#    "${CMAKE_CURRENT_BINARY_DIR}/libffmpeg"
#    "${libffmpeg_path}/avutil"
#    PUBLIC
#    "${libffmpeg_path}"
#    "${CMAKE_CURRENT_LIST_DIR}/libffmpeg.include"
#)
#target_compile_definitions(avutil PUBLIC
#        -DHAVE_AV_CONFIG_H=1
#        )
#
#
#add_library(avcodec)
#
#set_property(TARGET avcodec PROPERTY CXX_STANDARD 17)
#target_compile_features(avcodec PRIVATE cxx_std_17)
#
#file(GLOB avcodec_sources CONFIGURE_DEPENDS "${libffmpeg_path}/libavcodec/*.c")
#if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
#    file(GLOB avcodec_sources_x86 CONFIGURE_DEPENDS "${libffmpeg_path}/libavcodec/x86/*.c")
#    list(APPEND avcodec_sources ${avcodec_sources_x86})
#endif()
#list(REMOVE_ITEM avcodec_sources
#        "${libffmpeg_path}/libavcodec/aacpsdata.c"
#        "${libffmpeg_path}/libavcodec/aacps_tablegen.c"
#        "${libffmpeg_path}/libavcodec/ac3dec.c"
#        "${libffmpeg_path}/libavcodec/allcodecs.c"
#        "${libffmpeg_path}/libavcodec/beosthread.c"
#        "${libffmpeg_path}/libavcodec/binkaudio.c"
#        "${libffmpeg_path}/libavcodec/dv_tablegen.c"
#        "${libffmpeg_path}/libavcodec/dxva2.c"
#        "${libffmpeg_path}/libavcodec/dxva2_h264.c"
#        "${libffmpeg_path}/libavcodec/dxva2_vc1.c"
#        "${libffmpeg_path}/libavcodec/g729dec.c"
#        "${libffmpeg_path}/libavcodec/h264dspenc.c" # maybe not
#        "${libffmpeg_path}/libavcodec/h264_cabac.c" # maybe not
#        "${libffmpeg_path}/libavcodec/libdiracdec.c"
#        "${libffmpeg_path}/libavcodec/libdiracenc.c"
#        "${libffmpeg_path}/libavcodec/libfaac.c"
#        "${libffmpeg_path}/libavcodec/libfaad.c"
#        "${libffmpeg_path}/libavcodec/libgsm.c"
#        "${libffmpeg_path}/libavcodec/libmp3lame.c"
#        "${libffmpeg_path}/libavcodec/libopenjpeg.c"
#        "${libffmpeg_path}/libavcodec/libschroedinger.c"
#        "${libffmpeg_path}/libavcodec/libschroedingerdec.c"
#        "${libffmpeg_path}/libavcodec/libschroedingerenc.c"
#        "${libffmpeg_path}/libavcodec/libvpxdec.c"
#        "${libffmpeg_path}/libavcodec/libspeexdec.c"
#        "${libffmpeg_path}/libavcodec/libxvid_rc.c"
#        "${libffmpeg_path}/libavcodec/libvpxenc.c"
#        "${libffmpeg_path}/libavcodec/libtheoraenc.c"
#        "${libffmpeg_path}/libavcodec/libvorbis.c"
#        "${libffmpeg_path}/libavcodec/libxvidff.c"
#        "${libffmpeg_path}/libavcodec/libx264.c" # maybe not
#        "${libffmpeg_path}/libavcodec/motion_est_template.c"
#        "${libffmpeg_path}/libavcodec/mpegaudio_tablegen.c"
#        "${libffmpeg_path}/libavcodec/mpegvideo.c"
#        "${libffmpeg_path}/libavcodec/mpegvideo_xvmc.c"
#        "${libffmpeg_path}/libavcodec/mpegaudio.c"
#        "${libffmpeg_path}/libavcodec/mpegaudio_parser.c"
#        "${libffmpeg_path}/libavcodec/mpegaudio_tablegen.c"
#        "${libffmpeg_path}/libavcodec/mpegaudiodata.c"
#        "${libffmpeg_path}/libavcodec/mpegaudiodec.c"
#        "${libffmpeg_path}/libavcodec/mpegaudiodecheader.c"
#        "${libffmpeg_path}/libavcodec/mpegaudioenc.c"
#        "${libffmpeg_path}/libavcodec/mpegvideo.c"
#        "${libffmpeg_path}/libavcodec/mpegvideo_enc.c"
#        "${libffmpeg_path}/libavcodec/mpegvideo_parser.c"
#        "${libffmpeg_path}/libavcodec/mpegvideo_xvmc.c"
#        "${libffmpeg_path}/libavcodec/msmpeg4.c"
#        "${libffmpeg_path}/libavcodec/msmpeg4data.c"
#        "${libffmpeg_path}/libavcodec/os2thread.c"
#        "${libffmpeg_path}/libavcodec/vaapi.c"
#        "${libffmpeg_path}/libavcodec/vaapi_h264.c"
#        "${libffmpeg_path}/libavcodec/vaapi_mpeg2.c"
#        "${libffmpeg_path}/libavcodec/vaapi_mpeg4.c"
#        "${libffmpeg_path}/libavcodec/vaapi_vc1.c"
#        "${libffmpeg_path}/libavcodec/vdpau.c"
#        "${libffmpeg_path}/libavcodec/w32thread.c"
#        "${libffmpeg_path}/libavcodec/x86/dsputil_h264_template_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/dsputil_h264_template_ssse3.c"
#        "${libffmpeg_path}/libavcodec/x86/dsputil_mmx_avg_template.c"
#        "${libffmpeg_path}/libavcodec/x86/dsputil_mmx_qns_template.c"
#        "${libffmpeg_path}/libavcodec/x86/dsputil_mmx_rnd_template.c"
#        "${libffmpeg_path}/libavcodec/x86/mpegvideo_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/mpegvideo_mmx_template.c"
#        "${libffmpeg_path}/libavcodec/x86/motion_est_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/lpc_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/idct_sse2_xvid.c"
#        "${libffmpeg_path}/libavcodec/x86/h264dsp_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/rv40dsp_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/simple_idct_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/vc1dsp_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/vp3dsp_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/vp3dsp_mmx.h"
#        "${libffmpeg_path}/libavcodec/x86/vp3dsp_sse2.c"
#        "${libffmpeg_path}/libavcodec/x86/vp3dsp_sse2.h"
#        "${libffmpeg_path}/libavcodec/x86/vp6dsp_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/vp6dsp_mmx.h"
#        "${libffmpeg_path}/libavcodec/x86/vp6dsp_sse2.c"
#        "${libffmpeg_path}/libavcodec/x86/vp6dsp_sse2.h"
#        "${libffmpeg_path}/libavcodec/x86/snowdsp_mmx.c" #asm
#        "${libffmpeg_path}/libavcodec/x86/cavsdsp_mmx.c"
#        "${libffmpeg_path}/libavcodec/x86/dsputil_mmx.c"
#        )
#target_sources(avcodec PRIVATE ${avcodec_sources})
#target_include_directories(avcodec
#        PRIVATE
#        "${CMAKE_CURRENT_BINARY_DIR}/libffmpeg"
#        "${libffmpeg_path}/libavcodec"
#        "${libffmpeg_path}"
#)
#target_link_libraries(avcodec
#        PRIVATE
#        avutil
#)
#target_compile_definitions(avcodec PUBLIC
#        -DHAVE_AV_CONFIG_H=1
#        )
#
##target_compile_options(avcodec PRIVATE
##        $<$<COMPILE_LANGUAGE:ASM_NASM>:${CompileOptAsm}>
##        $<$<COMPILE_LANGUAGE:C>:${CompileOptC}>
##        $<$<COMPILE_LANGUAGE:CXX>:${CompileOptCxx}>
##        )
