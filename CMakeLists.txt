cmake_minimum_required(VERSION 3.7)
project(hela-surveillance-test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(src_path "android/hela-surveillance/app-surveillance-camera/src/main/cpp")

include(${src_path}/cmake/import_remote.cmake)
include("${src_path}/cmake/libopenssl.cmake")
#include("${src_path}/cmake/libliveMedia.cmake")
include("${src_path}/cmake/libdatachannel.cmake")

#add_subdirectory("${src_path}/src")

add_executable(test-rtc
	test/rtc/main.cpp

	${libdatachannel_path}/examples/streamer/fileparser.cpp
	${libdatachannel_path}/examples/streamer/h264fileparser.cpp
	${libdatachannel_path}/examples/streamer/opusfileparser.cpp
	${src_path}/src/rtc/helpers.cpp
	${src_path}/src/rtc/dispatch_queue.cpp
	${src_path}/src/rtc/stream.cpp
)

target_link_libraries(test-rtc
	datachannel
)

target_include_directories(test-rtc
	PRIVATE
	"${src_path}/src"
	"${libdatachannel_path}/examples/streamer"
)
