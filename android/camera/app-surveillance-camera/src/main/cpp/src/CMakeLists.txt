add_library(camera-lib SHARED
	lib.cpp
)

target_compile_features(camera-lib PUBLIC cxx_std_17)

target_link_libraries(camera-lib
	android
	log
	camera2ndk
	mediandk
	liveMedia
)

target_include_directories(camera-lib
	PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}"
)

set_target_properties(camera-lib PROPERTIES
	LINK_FLAGS_RELEASE -s
)
