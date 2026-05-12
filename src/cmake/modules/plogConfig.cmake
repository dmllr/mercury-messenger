if(NOT TARGET plog)
  message(FATAL_ERROR "The 'plog' target was not found. Please ensure that you have included 'libplog.cmake' before calling find_package(plog). This module is responsible for fetching and building the plog library.")
endif()

# Create the modern CMake alias target plog::plog for compatibility with find_package.
if(NOT TARGET plog::plog)
  add_library(plog::plog ALIAS plog)
endif()

# Set standard find_package variables to indicate success.
set(PLOG_FOUND TRUE)
set(PLOG_INCLUDE_DIR "${plog_path}/include")
set(plog_INCLUDE_DIR "${plog_path}/include")
