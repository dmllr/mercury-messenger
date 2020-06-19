add_library(camera-lib SHARED
	lib.cpp

	#datachannel-example
	rtc/dispatchqueue.cpp
#	rtc/h264fileparser.cpp
	rtc/helpers.cpp

	# temporary
	rtc/opusfileparser.cpp
	rtc/fileparser.cpp

	rtc/stream.cpp
)

link_libraries(crypto ssl)

target_compile_features(camera-lib PUBLIC cxx_std_17)

target_link_libraries(camera-lib
	android
	log
	camera2ndk
	mediandk
	liveMedia
	datachannel
)

target_include_directories(camera-lib
	PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}"
)
