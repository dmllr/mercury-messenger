include("../cmake/libdatachannel.cmake")
include("../cmake/libopus.cmake")
include("../cmake/libminiaudio.cmake")
include("../cmake/libjson.cmake")
if (NOT ANDROID)
  include("../cmake/libopencv.cmake")
  include("../cmake/libffmpeg.cmake")
endif ()

add_library(hela-eillance-core INTERFACE)
target_include_directories(hela-eillance-core INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_BINARY_DIR}"
)
target_link_libraries(hela-eillance-core INTERFACE
  libminiaudio
  datachannel-static
  opus
  plog
  nlohmann_json
  srtp2
)
if (NOT ANDROID)
  target_link_libraries(hela-eillance-core INTERFACE
    libopencv
    libffmpeg
  )
endif ()
if (WIN32)
  target_link_libraries(hela-eillance-core INTERFACE Ws2_32 Secur32 Bcrypt strmiids mfplat mfuuid)
endif ()
