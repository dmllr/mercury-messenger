add_library(eillance-logging INTERFACE)

set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

include("${shared_cmake_libs_path}/libplog.cmake")

target_include_directories(eillance-logging INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(eillance-logging INTERFACE
  plog
)

if(ANDROID)
  target_link_libraries(eillance-logging INTERFACE log)
endif()

target_compile_definitions(eillance-logging INTERFACE
  $<$<CONFIG:Release>:PLOG_DISABLE_LOGGING>
)
