cmake_minimum_required(VERSION 3.10.2)

# FFMPEG FETCH SECTION: START

set(FFMPEG_VERSION 6.0)
set(FFMPEG_URL https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2)
get_filename_component(FFMPEG_ARCHIVE_NAME ${FFMPEG_URL} NAME)
set(FFMPEG_PATH "${CMAKE_SOURCE_DIR}/_imported/ffmpeg-${FFMPEG_VERSION}")
set(FFMPEG_ARCHIVE_PATH "${CMAKE_SOURCE_DIR}/_imported/${FFMPEG_ARCHIVE_NAME}")
set(FFMPEG_WORKDIR ffmpeg)

IF (NOT EXISTS "${FFMPEG_PATH}")
    file(DOWNLOAD ${FFMPEG_URL} "${FFMPEG_ARCHIVE_PATH}")

    execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${FFMPEG_ARCHIVE_PATH}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/_imported"
    )

    # We're patching exit just before return in main function of ffmpeg.c because it will crash the application
    file(READ "${FFMPEG_PATH}/fftools/ffmpeg.c" ffmpeg_src)

    string(REPLACE "exit_program(received_nb_signals ? 255 : main_return_code);" "//exit_program(received_nb_signals ? 255 : main_return_code);" ffmpeg_src "${ffmpeg_src}")
    string(REPLACE "return main_return_code;" "return received_nb_signals ? 255 : main_return_code;" ffmpeg_src "${ffmpeg_src}")

    file(WRITE "${FFMPEG_PATH}/fftools/ffmpeg.c" "${ffmpeg_src}")
ENDIF()

#file(
#    COPY "${CMAKE_CURRENT_LIST_DIR}/ffmpeg_build.cmake"
#    DESTINATION "${FFMPEG_PATH}"
#    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
#)
file(COPY
        "${CMAKE_CURRENT_LIST_DIR}/config.h"
        "${CMAKE_CURRENT_LIST_DIR}/config_components.h"
    DESTINATION "${FFMPEG_PATH}"
)
file(REMOVE "${FFMPEG_PATH}/VERSION")

# FFMPEG FETCH SECTION: END

if (ANDROID)
    # We set custom variables for all build tools in case we want to make changes to in certain situations
    set(FFMPEG_CC ${CMAKE_C_COMPILER})
    set(FFMPEG_CXX ${CMAKE_CXX_COMPILER})
    set(FFMPEG_AR ${ANDROID_AR})
    set(FFMPEG_AS ${ANDROID_ASM_COMPILER})

    # Taken from https://android.googlesource.com/platform/ndk/+/master/build/cmake/android.toolchain.cmake
    # Remove when toolchain actually will include it
    set(FFMPEG_RANLIB ${ANDROID_TOOLCHAIN_PREFIX}ranlib${ANDROID_TOOLCHAIN_SUFFIX})
    set(FFMPEG_STRIP ${ANDROID_TOOLCHAIN_ROOT}/bin/llvm-strip${ANDROID_TOOLCHAIN_SUFFIX})

    # Set NM manually
    set(FFMPEG_NM ${ANDROID_TOOLCHAIN_PREFIX}nm${ANDROID_TOOLCHAIN_SUFFIX})

    # We remove --fatal-warnings in such blatant way because somebody at Google decided it's an excellent idea
    # to put it into LD flags in toolchain without any way to turn it off.
    string(REPLACE " -Wl,--fatal-warnings" "" FFMPEG_LD_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

    set(FFMPEG_C_FLAGS "${CMAKE_C_FLAGS} --target=${ANDROID_LLVM_TRIPLE} --gcc-toolchain=${ANDROID_TOOLCHAIN_ROOT} ${FFMPEG_EXTRA_C_FLAGS}")
    set(FFMPEG_ASM_FLAGS "${CMAKE_ASM_FLAGS} --target=${ANDROID_LLVM_TRIPLE} ${FFMPEG_EXTRA_ASM_FLAGS}")
    set(FFMPEG_LD_FLAGS "${FFMPEG_C_FLAGS} ${FFMPEG_LD_FLAGS} ${FFMPEG_EXTRA_LD_FLAGS}")

    set(HOST_BIN ${ANDROID_NDK}/prebuilt/${ANDROID_HOST_TAG}/bin)

    # https://trac.ffmpeg.org/ticket/4928 we must disable asm for x86 since it's non-PIC by design from FFmpeg side
    IF ("${CMAKE_ANDROID_ARCH_ABI}" STREQUAL "x86")
        list(APPEND FFMPEG_CONFIGURE_EXTRAS --disable-asm)
    ENDIF()

    set(CONFIGURE_PARAMS
#        --cc=${FFMPEG_CC}
#        --ar=${FFMPEG_AR}
#        --strip=${FFMPEG_STRIP}
#        --ranlib=${FFMPEG_RANLIB}
#        --as=${FFMPEG_AS}
#        --nm=${FFMPEG_NM}
#        --extra-cflags=${FFMPEG_C_FLAGS}
#        --extra-ldflags=${FFMPEG_LD_FLAGS}
        --target-os=android
        --arch=${ARCH}
        --enable-cross-compile
    )
endif()

ExternalProject_Add(ffmpeg_target
        PREFIX ${FFMPEG_WORKDIR}
        URL "${FFMPEG_PATH}"
        DOWNLOAD_NO_EXTRACT 1
        CONFIGURE_COMMAND ./configure
            --disable-programs
            --disable-doc
            --disable-static
            --enable-shared
            --enable-protocol=file
            --enable-pic
            ${CONFIGURE_PARAMS}
        BUILD_COMMAND make -j8
        BUILD_IN_SOURCE 1
        BUILD_BYPRODUCTS
            ${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target/libavfilter/libavfilter.so
            ${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target/libavcodec/libavcodec.so
            ${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target/libavdevice/libavdevice.so
            ${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target/libavformat/libavformat.so
            ${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target/libavutil/libavutil.so
            ${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target/libswresample/libswresample.so
            ${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target/libswscale/libswscale.so
        STEP_TARGETS copy_headers
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
)

#ExternalProject_Get_property(ffmpeg_target SOURCE_DIR)
#ExternalProject_Add_Step(
#        ffmpeg_target
#        copy_headers
#        COMMAND ${CMAKE_COMMAND}
#            -DBUILD_DIR:STRING=${SOURCE_DIR}
#            -DSOURCE_DIR:STRING=${CMAKE_CURRENT_SOURCE_DIR}
#            -DFFMPEG_NAME:STRING=${FFMPEG_NAME}
#            -DOUT:STRING=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
#        -P ${CMAKE_CURRENT_LIST_DIR}/copy_headers.cmake
#        DEPENDEES build
#        DEPENDERS install
#)

add_library(libffmpeg INTERFACE)
add_dependencies(libffmpeg ffmpeg_target)
target_include_directories(libffmpeg INTERFACE ${FFMPEG_PATH})
foreach(_lib libavfilter libavcodec libavdevice libavformat libavutil libswresample libswscale)
    target_link_libraries(libffmpeg INTERFACE
            ${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target/${_lib}/${_lib}.so)
endforeach()
if(0)
    target_link_libraries(libffmpeg INTERFACE
        lzma
        iconv
    )
endif()
if(APPLE)
    target_link_libraries(libffmpeg INTERFACE
        "-framework VideoToolbox"
        "-framework AudioToolbox"
    )
endif()
