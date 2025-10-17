include("../cmake/libdatachannel.cmake")
include("../cmake/libopus.cmake")
include("../cmake/libminiaudio.cmake")
include("../cmake/libjson.cmake")
if (NOT ANDROID)
  include("../cmake/libopencv.cmake")
  include("../cmake/libffmpeg.cmake")
endif ()


add_library(eillance-core INTERFACE)
target_include_directories(eillance-core INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_BINARY_DIR}"
)
target_link_libraries(eillance-core INTERFACE
  libminiaudio
  datachannel-static
  opus
  plog
  nlohmann_json
  srtp2
  eillance-utils
)
if (NOT ANDROID)
  target_link_libraries(eillance-core INTERFACE
    libopencv
    libffmpeg
  )
endif ()
if (WIN32)
  target_link_libraries(eillance-core INTERFACE Ws2_32 Secur32 Bcrypt strmiids mfplat mfuuid)
endif ()
if(ANDROID)
  find_library(log-lib log)
  target_link_libraries(eillance-core PRIVATE log-lib)
endif()
