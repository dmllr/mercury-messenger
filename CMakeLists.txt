cmake_minimum_required(VERSION 3.7)
project(hela-surveillance)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(src_path "android/hela-surveillance/app-surveillance-camera/src/main/cpp")

include(${src_path}/cmake/import_remote.cmake)
include("${src_path}/cmake/libopenssl.cmake")
#include("${src_path}/cmake/libliveMedia.cmake")
include("${src_path}/cmake/libdatachannel.cmake")
include("${src_path}/cmake/libopencv.cmake")
include("${src_path}/cmake/libffmpeg.cmake")

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

#find_package( OpenCV REQUIRED )
#MESSAGE(STATUS "Include dirs ${OpenCV_INCLUDE_DIRS}")
#MESSAGE(STATUS "LINK LIBRARIES ${OpenCV_LIBS}")


add_executable(mac
	macos/src/main.cpp

	#	${libdatachannel_path}/examples/streamer/fileparser.cpp
	#	${libdatachannel_path}/examples/streamer/h264fileparser.cpp
	#	${libdatachannel_path}/examples/streamer/opusfileparser.cpp
	#	${src_path}/src/rtc/helpers.cpp
	#	${src_path}/src/rtc/dispatch_queue.cpp
	#	${src_path}/src/rtc/stream.cpp
)
set_property(TARGET mac PROPERTY CXX_STANDARD 17)
target_compile_features(mac PRIVATE cxx_std_17)
target_compile_definitions(mac PUBLIC "-DBUILD_ZLIB=OFF")
target_link_libraries(mac PRIVATE
	opencv_core opencv_highgui opencv_imgproc opencv_videoio
#	${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${AVDEVICE_LIBRARY}
#	PkgConfig::LIBAV
	${AVUTIL_LIBRARY} ${AVCODEC_LIBRARY} ${SWSCALE_LIBRARY}
#		avcodec
#	datachannel
)
target_include_directories(mac
	PRIVATE
	"/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
#	"/Library/Developer/CommandLineTools/SDKs/MacOSX12.1.sdk/usr/include/c++/v1/"

	"${src_path}/src"
	"${libopencv_path}/include"
	"${libopencv_path}/modules/core/include"
#	"${libopencv_path}/modules/calib3d/include"
#	"${libopencv_path}/modules/features2d/include"
	"${libopencv_path}/modules/imgcodecs/include"
	"${libopencv_path}/modules/imgproc/include"
	"${libopencv_path}/modules/highgui/include"
	"${libopencv_path}/modules/videoio/include"

#	"${libffmpeg_path}"
	${AVUTIL_INCLUDE_DIR}
	${AVCODEC_INCLUDE_DIR}
	${SWSCALE_INCLUDE_DIR}

	"${CMAKE_CURRENT_BINARY_DIR}"

)
target_link_directories(mac PRIVATE /usr/local/Cellar/ffmpeg/5.1.2/lib)
