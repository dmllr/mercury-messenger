set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
import_remote("https://github.com/paullouisageneau/libjuice.git" TAG "v0.9.8" DST "${CMAKE_SOURCE_DIR}/_imported/libjuice" ALIAS libjuice)
