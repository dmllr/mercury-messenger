if (TARGET libminiaudio)
  return()
endif()

import_remote("https://github.com/mackron/miniaudio.git" TAG "0.11.21" DST "${REPO_ROOT}/_imported" NO_CMAKE ALIAS libminiaudio)

add_library(libminiaudio INTERFACE
  "${libminiaudio_path}/miniaudio.h"
)
target_include_directories(libminiaudio INTERFACE
  "${libminiaudio_path}"
)
target_compile_definitions(libminiaudio INTERFACE
  MINIAUDIO_IMPLEMENTATION
  MA_NO_DECODING
  MA_NO_ENCODING
  MA_NO_GENERATION
  MA_NO_RESOURCE_MANAGER
  MA_NO_ENGINE
)

add_library(libminiaudio-full INTERFACE
  "${libminiaudio_path}/miniaudio.h"
)
target_include_directories(libminiaudio-full INTERFACE
  "${libminiaudio_path}"
)
target_compile_definitions(libminiaudio-full INTERFACE
  MINIAUDIO_IMPLEMENTATION
)
