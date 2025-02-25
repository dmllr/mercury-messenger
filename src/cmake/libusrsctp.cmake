option(sctp_build_programs "Build example programs" 0)
option(sctp_build_shared_lib "Build USRSCTP as shared library" 0)

set(sctp_build_programs 0)
set(sctp_build_shared_lib 0)

import_remote("https://github.com/sctplab/usrsctp.git" TAG "master" DST "${CMAKE_SOURCE_DIR}/_imported" ALIAS usrsctp)
