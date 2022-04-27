add_library(camera-lib SHARED
	lib.cpp

	audio/audio_common.cpp
	audio/audio_recorder.cpp

		rtc/dispatch_queue.cpp
	rtc/helpers.cpp
	rtc/stream.cpp
)

target_compile_features(camera-lib PUBLIC cxx_std_17)

target_link_libraries(camera-lib
	android
	log
	camera2ndk
	mediandk
	OpenSLES
	liveMedia
	datachannel
)

target_include_directories(camera-lib
	PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}"
)
