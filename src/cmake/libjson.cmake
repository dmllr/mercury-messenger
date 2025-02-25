SET(JSON_BuildTests OFF CACHE BOOL "Build the unit tests when BUILD_TESTING is enabled.")
SET(JSON_Install OFF CACHE BOOL "Install CMake targets during install step.")
import_remote("https://github.com/nlohmann/json.git" TAG "master" DST "${CMAKE_SOURCE_DIR}/_imported" ALIAS json)
