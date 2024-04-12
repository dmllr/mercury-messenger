set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF)
import_remote("https://gitlab.xiph.org/xiph/opus.git" TAG "v1.3.1" DST "${CMAKE_SOURCE_DIR}/_imported/libopus" ALIAS opus)
