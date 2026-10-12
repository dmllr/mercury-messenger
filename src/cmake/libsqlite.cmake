include(FetchContent)

set(SQLITE3_VERSION "3.50.4")
set(SQLITE3_AMALGAMATION_VERSION "3500400") # As per the zip file name
set(SQLITE3_URL "https://sqlite.org/2025/sqlite-amalgamation-${SQLITE3_AMALGAMATION_VERSION}.zip")
set(SQLITE3_URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032)

FetchContent_Declare(
  sqlite3_source
  URL ${SQLITE3_URL}
  URL_HASH ${SQLITE3_URL_HASH}
)

FetchContent_MakeAvailable(sqlite3_source)

add_library(SQLite3 STATIC
  ${sqlite3_source_SOURCE_DIR}/sqlite3.c
  ${sqlite3_source_SOURCE_DIR}/sqlite3.h
)
target_include_directories(SQLite3 PUBLIC ${sqlite3_source_SOURCE_DIR})
target_compile_definitions(SQLite3 PUBLIC SQLITE_ENABLE_COLUMN_METADATA)
if(ANDROID)
  target_link_libraries(SQLite3 PUBLIC log)
endif()

if(NOT TARGET SQLite::SQLite3)
  add_library(SQLite::SQLite3 ALIAS SQLite3)
endif()

# Set standard find_package variables to indicate success.
set(SQLite3_FOUND TRUE)
set(SQLite3_INCLUDE_DIR ${sqlite3_source_SOURCE_DIR} CACHE PATH "Path to downloaded SQLite3 headers" FORCE)
set(SQLite3_LIBRARY SQLite3 CACHE STRING "SQLite3 library target" FORCE)
