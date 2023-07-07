set (root ../../../../../../../../..)
set (shared_libs ${root}/src/lib)

add_library(camera-lib SHARED
	lib.cpp
	audio/recorder.h
	audio/recorder.cpp
	audio/audio_common.h
	audio/audio_common.cpp

	${shared_libs}/rtc/dispatch_queue.hpp
	${shared_libs}/rtc/dispatch_queue.cpp
	${shared_libs}/rtc/helpers.hpp
	${shared_libs}/rtc/helpers.cpp
	${shared_libs}/rtc/stream.hpp
	${shared_libs}/rtc/stream.cpp
)

add_dependencies(camera-lib ffmpeg_target)

target_compile_features(camera-lib PUBLIC cxx_std_17)

target_link_libraries(camera-lib
	android
	log
	camera2ndk
	mediandk
	aaudio
	liveMedia
	ssl
	crypto
	datachannel
	opus

	${OpenCV_LIBS}

	libffmpeg
)

target_include_directories(camera-lib
	PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}"
	"${shared_libs}"
	"${shared_libs}/../utils"

	"${OpenCV_INCLUDE_DIRS}"
)
