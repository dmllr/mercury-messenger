import_remote("https://github.com/mackron/miniaudio.git" TAG "0.11.21" DST "${CMAKE_SOURCE_DIR}/_imported/libminiaudio" NO_CMAKE ALIAS libminiaudio)

add_library(libminiaudio INTERFACE
  "${libminiaudio_path}/miniaudio.h"
  "${CMAKE_CURRENT_LIST_DIR}/libminiaudio.files/miniaudio.c"
)
target_include_directories(libminiaudio INTERFACE
  "${libminiaudio_path}"
)
