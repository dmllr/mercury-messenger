cmake_minimum_required(VERSION 3.10.2)

file(GLOB libs ${CMAKE_SOURCE_DIR}/_imported/${FFMPEG_NAME}/lib*)
file(
    COPY ${libs} ${CMAKE_CURRENT_LIST_DIR}/config.h ${CMAKE_SOURCE_DIR}/_imported/${FFMPEG_NAME}/compat
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/include
    FILES_MATCHING PATTERN *.h
)
