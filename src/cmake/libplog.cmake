if(TARGET plog)
	return()
endif()

set(PLOG_BUILD_TESTS OFF CACHE BOOL "Disable plog tests" FORCE)
set(PLOG_BUILD_SAMPLES OFF CACHE BOOL "Disable plog samples" FORCE)
set(PLOG_INSTALL OFF CACHE BOOL "Disable plog install" FORCE)

import_remote("https://github.com/SergiusTheBest/plog.git"
	TAG "1.1.11"
	DST "${REPO_ROOT}/_imported"
	ALIAS plog
)
