import_remote("https://github.com/rgaufman/live555.git" TAG "master" DST "${REPO_ROOT}/_imported" ALIAS live555 NO_CMAKE)

file(GLOB LIVE_MEDIA_SRCS
  "${live555_path}/liveMedia/*.c"
  "${live555_path}/liveMedia/*.cpp"
  "${live555_path}/groupsock/*.c"
  "${live555_path}/groupsock/*.cpp"
  "${live555_path}/UsageEnvironment/*.cpp"
  "${live555_path}/BasicUsageEnvironment/*.cpp"
)

add_library(liveMedia STATIC
  ${LIVE_MEDIA_SRCS}
)

target_compile_definitions(liveMedia PUBLIC
  SOCKLEN_T=socklen_t
)

target_include_directories(liveMedia
  PUBLIC
  "${live555_path}/BasicUsageEnvironment/include"
  "${live555_path}/UsageEnvironment/include"
  "${live555_path}/groupsock/include"
  "${live555_path}/liveMedia/include"
)

target_link_libraries(liveMedia
  ssl
  crypto
)
