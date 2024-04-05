set(CMAKE_PROJECT_VERSION "v0.20.2")
set(CMAKE_PROJECT_NAME "libdatachannel")

# for libdatachannel
set(USE_MBEDTLS ON)
set(USE_GNUTLS OFF)
set(NO_TESTS ON)
set(NO_EXAMPLES ON)

# for libdatachannel -> usrsctp
set(ENABLE_MBEDTLS ON)
set(ENABLE_OPENSSL OFF)
set(ENABLE_TESTING OFF)
set(ENABLE_PROGRAMS OFF)

option(sctp_build_programs "Build example programs" 0)
option(sctp_build_shared_lib "Build USRSCTP as shared library" 0)

set(sctp_build_programs 0)
set(sctp_build_shared_lib 0)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# import
import_remote("https://github.com/paullouisageneau/libdatachannel.git" TAG "${CMAKE_PROJECT_VERSION}" DST "${CMAKE_SOURCE_DIR}/_imported/libdatachannel" ALIAS libdatachannel)
