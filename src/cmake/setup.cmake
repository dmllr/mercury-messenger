# Setup CMake for the project

if(MSVC)
  # Set the desired static runtime library for all targets in the project.
  # This uses a generator expression to select MultiThreadedDebug (/MTd) for Debug builds
  # and MultiThreaded (/MT) for Release builds, ensuring consistency with dependencies.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

  # Some sub-projects or CMake defaults might add the dynamic runtime flags (/MD or /MDd).
  # To prevent D9025 override warnings, we explicitly remove these flags from the compiler
  # flag variables, allowing CMAKE_MSVC_RUNTIME_LIBRARY to be the single source of truth.
  string(REGEX REPLACE "/MDd?" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REGEX REPLACE "/MDd?" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

enable_language(C)
enable_language(CXX)

set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

## Uncomment for profiling
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")

# Policies
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
cmake_policy(SET CMP0063 NEW)
cmake_policy(SET CMP0077 NEW)
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()

# Custom modules
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")

# Allow dependencies using find_package(OpenSSL) and it will use the custom module
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
# Prevent find_package from finding system OpenSSL
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
