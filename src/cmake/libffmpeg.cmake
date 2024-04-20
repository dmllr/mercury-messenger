# reference:
# https://github.com/IljaKosynkin/FFmpeg-Development-Kit/tree/master/ffmpeg-sample/app/src/main/cpp
# LIB_X264 is not included

link_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
include_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/libffmpeg.include)

include(ExternalProject)

set(FFMPEG_LIBS avutil swresample avcodec avformat swscale avfilter avdevice)
set(FFMPEG_CONFIGURE_EXTRAS --enable-jni)


set(FFMPEG_VERSION 6.1.1)
set(FFMPEG_URL https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2)
get_filename_component(FFMPEG_ARCHIVE_NAME ${FFMPEG_URL} NAME)
set(FFMPEG_PATH "${CMAKE_SOURCE_DIR}/_imported/ffmpeg-${FFMPEG_VERSION}")
set(FFMPEG_ARCHIVE_PATH "${CMAKE_SOURCE_DIR}/_imported/${FFMPEG_ARCHIVE_NAME}")
set(FFMPEG_WORKDIR ffmpeg)

if (NOT EXISTS "${FFMPEG_PATH}")
  file(DOWNLOAD ${FFMPEG_URL} "${FFMPEG_ARCHIVE_PATH}")

  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xzf "${FFMPEG_ARCHIVE_PATH}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/_imported"
  )

  # We're patching exit just before return in main function of ffmpeg.c because it will crash the application
  file(READ "${FFMPEG_PATH}/fftools/ffmpeg.c" code)
  string(REPLACE "exit_program(received_nb_signals ? 255 : main_return_code);" "//exit_program(received_nb_signals ? 255 : main_return_code);" code "${code}")
  string(REPLACE "return main_return_code;" "return received_nb_signals ? 255 : main_return_code;" code "${code}")
  file(WRITE "${FFMPEG_PATH}/fftools/ffmpeg.c" "${code}")

  file(REMOVE "${FFMPEG_PATH}/VERSION")
endif ()

if (ANDROID)
  # We set custom variables for all build tools in case we want to make changes to in certain situations
  set(FFMPEG_CC ${CMAKE_C_COMPILER})
  set(FFMPEG_CXX ${CMAKE_CXX_COMPILER})
  set(FFMPEG_AR ${ANDROID_AR})
  set(FFMPEG_AS ${ANDROID_ASM_COMPILER})

  # Inspired by https://android.googlesource.com/platform/ndk/+/master/build/cmake/android.toolchain.cmake,
  # remove when toolchain actually will include it
  set(FFMPEG_RANLIB ${ANDROID_TOOLCHAIN_ROOT}/bin/llvm-ranlib${ANDROID_TOOLCHAIN_SUFFIX})
  set(FFMPEG_STRIP ${ANDROID_TOOLCHAIN_ROOT}/bin/llvm-strip${ANDROID_TOOLCHAIN_SUFFIX})
  set(FFMPEG_NM ${ANDROID_TOOLCHAIN_ROOT}/bin/llvm-nm${ANDROID_TOOLCHAIN_SUFFIX})

  # Remove --fatal-warnings in such blatant way because somebody at Google decided it's an excellent idea
  # to put it into LD flags in toolchain without any way to turn it off.
  string(REPLACE " -Wl,--fatal-warnings" "" FFMPEG_LD_FLAGS ${CMAKE_SHARED_LINKER_FLAGS})

  set(FFMPEG_C_FLAGS "${CMAKE_C_FLAGS} --target=${ANDROID_LLVM_TRIPLE} --gcc-toolchain=${ANDROID_TOOLCHAIN_ROOT} ${FFMPEG_EXTRA_C_FLAGS}")
  set(FFMPEG_CXX_FLAGS "${CMAKE_CXX_FLAGS} --target=${ANDROID_LLVM_TRIPLE} --gcc-toolchain=${ANDROID_TOOLCHAIN_ROOT} ${FFMPEG_EXTRA_CXX_FLAGS}")
  set(FFMPEG_ASM_FLAGS "${CMAKE_ASM_FLAGS} --target=${ANDROID_LLVM_TRIPLE} ${FFMPEG_EXTRA_ASM_FLAGS}")
  set(FFMPEG_LD_FLAGS "${FFMPEG_C_FLAGS} ${FFMPEG_LD_FLAGS} ${FFMPEG_EXTRA_LD_FLAGS}")

  set(HOST_BIN ${ANDROID_NDK}/prebuilt/${ANDROID_HOST_TAG}/bin)

  set(CONFIGURE_PARAMS
    --cc=${FFMPEG_CC}
    --ar=${FFMPEG_AR}
    --strip=${FFMPEG_STRIP}
    --ranlib=${FFMPEG_RANLIB}
    --as=${FFMPEG_AS}
    --nm=${FFMPEG_NM}
    --target-os=android
    --arch=${CMAKE_SYSTEM_PROCESSOR}
    --extra-cflags=${FFMPEG_C_FLAGS}
    --extra-cxxflags=${FFMPEG_CXX_FLAGS}
    --extra-ldflags=${FFMPEG_LD_FLAGS}
    --sysroot=${CMAKE_SYSROOT}
    --enable-cross-compile
    --enable-pic
#    --enable-mediacodec
#    --enable-jni
  )

  # https://trac.ffmpeg.org/ticket/4928
  # we must disable asm for x86 since it's non-PIC by design from FFmpeg side
  IF ("${CMAKE_ANDROID_ARCH_ABI}" STREQUAL "x86")
    list(APPEND CONFIGURE_PARAMS --disable-asm)
  ENDIF ()
endif ()

if (WIN32)
  set(CONFIGURE_PARAMS
    --toolchain=msvc
    --enable-w32threads
  )
endif ()

set(ffmpeg_libraries "avfilter;avcodec;avdevice;avformat;avutil;swresample;swscale")

set(products "")
foreach (_lib avfilter avcodec avdevice avformat avutil swresample swscale)
  list(APPEND products
    "${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target/lib${_lib}/lib${_lib}.a")
endforeach ()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  list(APPEND CONFIGURE_PARAMS "--enable-x86asm")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  list(APPEND CONFIGURE_PARAMS "--disable-x86asm")
else()
  message(FATAL_ERROR "Unsupported processor: ${CMAKE_SYSTEM_PROCESSOR}")
endif()


# requires "sh" and other commands to be available
# MSYS2 must be installed on Windows. See /README.md for the instructions
ExternalProject_Add(ffmpeg_target
  PREFIX ${FFMPEG_WORKDIR}
  URL "${FFMPEG_PATH}"
  DOWNLOAD_NO_EXTRACT 1
  CONFIGURE_COMMAND
    sh ./configure
      --disable-network
      --disable-protocols
      --disable-filters
      --disable-doc
      --disable-debug
      --disable-devices
      --disable-parsers
      --disable-decoders
      --disable-shared
      --enable-static
      --enable-encoder=h264
      --enable-decoder=h264
      --enable-encoder=rawvideo
      --enable-gpl
      # --enable-libx264 // not sure it is required, anyway the library should be built/installed
      ${CONFIGURE_PARAMS}
  BUILD_COMMAND make -j${NJOBS}
  INSTALL_COMMAND ""
  BUILD_IN_SOURCE 1
  BUILD_BYPRODUCTS
    ${products}
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1
)

add_library(libffmpeg INTERFACE)
add_dependencies(libffmpeg ffmpeg_target)
target_include_directories(libffmpeg INTERFACE
  "${FFMPEG_PATH}"
  "${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_WORKDIR}/src/ffmpeg_target"
)
foreach (_lib IN LISTS products)
  target_link_libraries(libffmpeg INTERFACE "${_lib}")
endforeach ()

if (UNIX AND NOT ANDROID)
  target_link_libraries(libffmpeg INTERFACE
    lzma
    iconv
  )
  if (APPLE)
    target_link_libraries(libffmpeg INTERFACE
      "-framework VideoToolbox"
      "-framework AudioToolbox"
    )
  endif ()
endif ()
