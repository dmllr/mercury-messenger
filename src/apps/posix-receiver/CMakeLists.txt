set(src_path "../..")

add_executable(posix-receiver
	main.cpp

	${libdatachannel_path}/examples/streamer/fileparser.cpp
	${libdatachannel_path}/examples/streamer/h264fileparser.cpp
	${libdatachannel_path}/examples/streamer/opusfileparser.cpp

	${src_path}/lib/rtc/dispatch_queue.cpp
	${src_path}/lib/rtc/stream.cpp

	${ffmpeg_src}
)
set_property(TARGET posix-receiver PROPERTY CXX_STANDARD 17)
target_compile_features(posix-receiver PRIVATE cxx_std_17)
target_compile_definitions(posix-receiver PUBLIC "-DBUILD_ZLIB=OFF")
target_link_libraries(posix-receiver PRIVATE
	opencv_core opencv_highgui opencv_imgproc opencv_videoio
	libffmpeg
	datachannel
)
target_include_directories(posix-receiver
	PRIVATE
	"/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"

	"${src_path}/lib"
	"${libopencv_path}/include"
	"${libopencv_path}/modules/core/include"
	"${libopencv_path}/modules/imgcodecs/include"
	"${libopencv_path}/modules/imgproc/include"
	"${libopencv_path}/modules/highgui/include"
	"${libopencv_path}/modules/videoio/include"

	# for OpenCV generated files
	"${CMAKE_BINARY_DIR}"

	# tmp
	"${libdatachannel_path}/examples/streamer"
)
