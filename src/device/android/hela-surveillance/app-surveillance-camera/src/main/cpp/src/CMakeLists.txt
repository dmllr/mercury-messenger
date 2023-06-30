add_library(camera-lib SHARED
	lib.cpp
	audio/recorder.h
	audio/recorder.cpp
	audio/audio_common.h
	audio/audio_common.cpp

	rtc/dispatch_queue.hpp
	rtc/dispatch_queue.cpp
	rtc/helpers.hpp
	rtc/helpers.cpp
	rtc/stream.hpp
	rtc/stream.cpp
)

target_compile_features(camera-lib PUBLIC cxx_std_17)

target_link_libraries(camera-lib
	android
	log
	camera2ndk
	mediandk
	aaudio
	liveMedia
	datachannel
	opus
)

target_include_directories(camera-lib
	PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}"
)
