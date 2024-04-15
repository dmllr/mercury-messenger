if(NOT DEFINED CMAKE_PROJECT_VERSION OR CMAKE_PROJECT_VERSION STREQUAL "")
	set(CMAKE_PROJECT_VERSION "1.0.0")
endif()

set(sctp_build_programs OFF)
set(sctp_build_shared_lib OFF)
set(sctp_werror OFF)
set(ENABLE_WARNINGS_AS_ERRORS OFF)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fms-extensions")

import_remote("https://github.com/cisco/libsrtp.git"
	TAG "v2.6.0"
	DST "${CMAKE_SOURCE_DIR}/_imported/libsrtp"
	PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/libsrtp.patch"
	ALIAS libsrtp )
