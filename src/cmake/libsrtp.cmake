if(NOT DEFINED CMAKE_PROJECT_VERSION OR CMAKE_PROJECT_VERSION STREQUAL "")
	set(CMAKE_PROJECT_VERSION "1.0.0")
endif()

import_remote("https://github.com/cisco/libsrtp.git" TAG "v2.4.2" DST "${CMAKE_SOURCE_DIR}/_imported/libsrtp" ALIAS libsrtp )
