cmake_minimum_required(VERSION 3.7)
project(hela-surveillance)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(src_path "src")

include(${src_path}/cmake/import_remote.cmake)
include("${src_path}/cmake/libssl.cmake")
include("${src_path}/cmake/libdatachannel.cmake")
include("${src_path}/cmake/libopencv.cmake")
include("${src_path}/cmake/libffmpeg.cmake")


add_executable(test-rtc
	test/rtc/main.cpp

	${libdatachannel_path}/examples/streamer/fileparser.cpp
	${libdatachannel_path}/examples/streamer/h264fileparser.cpp
	${libdatachannel_path}/examples/streamer/opusfileparser.cpp
	${src_path}/lib/rtc/helpers.cpp
	${src_path}/lib/rtc/dispatch_queue.cpp
	${src_path}/lib/rtc/stream.cpp
)

target_link_libraries(test-rtc
	datachannel
)

target_include_directories(test-rtc
	PRIVATE
	"${src_path}/lib"
	"${libdatachannel_path}/examples/streamer"
)

add_executable(mac
	src/device/general/main.cpp

	${libdatachannel_path}/examples/streamer/fileparser.cpp
	${libdatachannel_path}/examples/streamer/h264fileparser.cpp
	${libdatachannel_path}/examples/streamer/opusfileparser.cpp
	${src_path}/lib/rtc/helpers.cpp
	${src_path}/lib/rtc/dispatch_queue.cpp
	${src_path}/lib/rtc/stream.cpp
)
set_property(TARGET mac PROPERTY CXX_STANDARD 17)
target_compile_features(mac PRIVATE cxx_std_17)
target_compile_definitions(mac PUBLIC "-DBUILD_ZLIB=OFF")
target_link_libraries(mac PRIVATE
	opencv_core opencv_highgui opencv_imgproc opencv_videoio
	${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES}
	datachannel
)
target_include_directories(mac
	PRIVATE
	"/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"

	"${src_path}/lib"
	"${src_path}/utils"
	"${libopencv_path}/include"
	"${libopencv_path}/modules/core/include"
	"${libopencv_path}/modules/imgcodecs/include"
	"${libopencv_path}/modules/imgproc/include"
	"${libopencv_path}/modules/highgui/include"
	"${libopencv_path}/modules/videoio/include"

	${AVCODEC_INCLUDE_DIRS}
	${AVFORMAT_INCLUDE_DIRS}
	${AVUTIL_INCLUDE_DIRS}
	${SWSCALE_INCLUDE_DIRS}

	"${CMAKE_CURRENT_BINARY_DIR}"

	# tmp
	"${libdatachannel_path}/examples/streamer"
)
target_link_directories(mac PRIVATE /usr/local/Cellar/ffmpeg/6.0/lib)
