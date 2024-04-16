set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF)
set(OPUS_BUILD_PROGRAMS OFF)
import_remote("https://gitlab.xiph.org/xiph/opus.git" TAG "v1.5.2" DST "${CMAKE_SOURCE_DIR}/_imported/libopus" ALIAS opus)
