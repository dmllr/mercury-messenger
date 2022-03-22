import_remote("https://github.com/paullouisageneau/libdatachannel.git" TAG "fa18a6bae6c7efcc23ff0722e0f7c4668ca0e6e0" DST "${CMAKE_SOURCE_DIR}/_imported/libdatachannel" ALIAS libdatachannel NO_CMAKE)
# about v0.16.6

#set(libdatachannel_path ${CMAKE_CURRENT_LIST_DIR}/../libs/datachannel)

set(BUILD_SHARED_LIBS OFF) # to force usrsctp to be built static

include(${CMAKE_CURRENT_LIST_DIR}/libplog.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libjuice.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libusrsctp.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libjson.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/libsrtp.cmake)


set(LIBDATACHANNEL_SOURCES
	${libdatachannel_path}/src/candidate.cpp
	${libdatachannel_path}/src/channel.cpp
	${libdatachannel_path}/src/configuration.cpp
	${libdatachannel_path}/src/datachannel.cpp
	${libdatachannel_path}/src/description.cpp
	${libdatachannel_path}/src/global.cpp
	${libdatachannel_path}/src/message.cpp
	${libdatachannel_path}/src/peerconnection.cpp
	${libdatachannel_path}/src/rtcpreceivingsession.cpp
	${libdatachannel_path}/src/track.cpp
	${libdatachannel_path}/src/websocket.cpp
	${libdatachannel_path}/src/websocketserver.cpp
	${libdatachannel_path}/src/rtppacketizationconfig.cpp
	${libdatachannel_path}/src/rtcpsrreporter.cpp
	${libdatachannel_path}/src/rtppacketizer.cpp
	${libdatachannel_path}/src/opusrtppacketizer.cpp
	${libdatachannel_path}/src/opuspacketizationhandler.cpp
	${libdatachannel_path}/src/h264rtppacketizer.cpp
	${libdatachannel_path}/src/nalunit.cpp
	${libdatachannel_path}/src/h264packetizationhandler.cpp
	${libdatachannel_path}/src/mediachainablehandler.cpp
	${libdatachannel_path}/src/mediahandlerelement.cpp
	${libdatachannel_path}/src/mediahandlerrootelement.cpp
	${libdatachannel_path}/src/rtcpnackresponder.cpp
	${libdatachannel_path}/src/rtp.cpp
	${libdatachannel_path}/src/capi.cpp
)

set(LIBDATACHANNEL_HEADERS
	${libdatachannel_path}/include/rtc/candidate.hpp
	${libdatachannel_path}/include/rtc/channel.hpp
	${libdatachannel_path}/include/rtc/configuration.hpp
	${libdatachannel_path}/include/rtc/datachannel.hpp
	${libdatachannel_path}/include/rtc/description.hpp
	${libdatachannel_path}/include/rtc/mediahandler.hpp
	${libdatachannel_path}/include/rtc/rtcpreceivingsession.hpp
	${libdatachannel_path}/include/rtc/common.hpp
	${libdatachannel_path}/include/rtc/global.hpp
	${libdatachannel_path}/include/rtc/message.hpp
	${libdatachannel_path}/include/rtc/peerconnection.hpp
	${libdatachannel_path}/include/rtc/reliability.hpp
	${libdatachannel_path}/include/rtc/rtc.h
	${libdatachannel_path}/include/rtc/rtc.hpp
	${libdatachannel_path}/include/rtc/rtp.hpp
	${libdatachannel_path}/include/rtc/track.hpp
	${libdatachannel_path}/include/rtc/websocket.hpp
	${libdatachannel_path}/include/rtc/websocketserver.hpp
	${libdatachannel_path}/include/rtc/rtppacketizationconfig.hpp
	${libdatachannel_path}/include/rtc/rtcpsrreporter.hpp
	${libdatachannel_path}/include/rtc/rtppacketizer.hpp
	${libdatachannel_path}/include/rtc/opusrtppacketizer.hpp
	${libdatachannel_path}/include/rtc/opuspacketizationhandler.hpp
	${libdatachannel_path}/include/rtc/h264rtppacketizer.hpp
	${libdatachannel_path}/include/rtc/nalunit.hpp
	${libdatachannel_path}/include/rtc/h264packetizationhandler.hpp
	${libdatachannel_path}/include/rtc/mediachainablehandler.hpp
	${libdatachannel_path}/include/rtc/mediahandlerelement.hpp
	${libdatachannel_path}/include/rtc/mediahandlerrootelement.hpp
	${libdatachannel_path}/include/rtc/rtcpnackresponder.hpp
	${libdatachannel_path}/include/rtc/utils.hpp
)

set(LIBDATACHANNEL_IMPL_SOURCES
	${libdatachannel_path}/src/impl/certificate.cpp
	${libdatachannel_path}/src/impl/channel.cpp
	${libdatachannel_path}/src/impl/datachannel.cpp
	${libdatachannel_path}/src/impl/dtlssrtptransport.cpp
	${libdatachannel_path}/src/impl/dtlstransport.cpp
	${libdatachannel_path}/src/impl/icetransport.cpp
	${libdatachannel_path}/src/impl/init.cpp
	${libdatachannel_path}/src/impl/peerconnection.cpp
	${libdatachannel_path}/src/impl/logcounter.cpp
	${libdatachannel_path}/src/impl/sctptransport.cpp
	${libdatachannel_path}/src/impl/threadpool.cpp
	${libdatachannel_path}/src/impl/tls.cpp
	${libdatachannel_path}/src/impl/track.cpp
	${libdatachannel_path}/src/impl/utils.cpp
	${libdatachannel_path}/src/impl/processor.cpp
	${libdatachannel_path}/src/impl/base64.cpp
	${libdatachannel_path}/src/impl/sha.cpp
	${libdatachannel_path}/src/impl/pollinterrupter.cpp
	${libdatachannel_path}/src/impl/pollservice.cpp
	${libdatachannel_path}/src/impl/tcpserver.cpp
	${libdatachannel_path}/src/impl/tcptransport.cpp
	${libdatachannel_path}/src/impl/tlstransport.cpp
	${libdatachannel_path}/src/impl/verifiedtlstransport.cpp
	${libdatachannel_path}/src/impl/websocket.cpp
	${libdatachannel_path}/src/impl/websocketserver.cpp
	${libdatachannel_path}/src/impl/wstransport.cpp
	${libdatachannel_path}/src/impl/wshandshake.cpp
)

set(LIBDATACHANNEL_IMPL_HEADERS
	${libdatachannel_path}/src/impl/certificate.hpp
	${libdatachannel_path}/src/impl/channel.hpp
	${libdatachannel_path}/src/impl/datachannel.hpp
	${libdatachannel_path}/src/impl/dtlssrtptransport.hpp
	${libdatachannel_path}/src/impl/dtlstransport.hpp
	${libdatachannel_path}/src/impl/icetransport.hpp
	${libdatachannel_path}/src/impl/init.hpp
	${libdatachannel_path}/src/impl/internals.hpp
	${libdatachannel_path}/src/impl/peerconnection.hpp
	${libdatachannel_path}/src/impl/queue.hpp
	${libdatachannel_path}/src/impl/logcounter.hpp
	${libdatachannel_path}/src/impl/sctptransport.hpp
	${libdatachannel_path}/src/impl/threadpool.hpp
	${libdatachannel_path}/src/impl/tls.hpp
	${libdatachannel_path}/src/impl/track.hpp
	${libdatachannel_path}/src/impl/utils.hpp
	${libdatachannel_path}/src/impl/processor.hpp
	${libdatachannel_path}/src/impl/base64.hpp
	${libdatachannel_path}/src/impl/sha.hpp
	${libdatachannel_path}/src/impl/pollinterrupter.hpp
	${libdatachannel_path}/src/impl/pollservice.hpp
	${libdatachannel_path}/src/impl/tcpserver.hpp
	${libdatachannel_path}/src/impl/tcptransport.hpp
	${libdatachannel_path}/src/impl/tlstransport.hpp
	${libdatachannel_path}/src/impl/verifiedtlstransport.hpp
	${libdatachannel_path}/src/impl/websocket.hpp
	${libdatachannel_path}/src/impl/websocketserver.hpp
	${libdatachannel_path}/src/impl/wstransport.hpp
	${libdatachannel_path}/src/impl/wshandshake.hpp
)

add_library(datachannel STATIC
	${LIBDATACHANNEL_SOURCES}
	${LIBDATACHANNEL_HEADERS}
	${LIBDATACHANNEL_IMPL_SOURCES}
	${LIBDATACHANNEL_IMPL_HEADERS}
)
target_include_directories(datachannel
	PUBLIC
	${libdatachannel_path}/include
	PRIVATE
	${usrsctp_path}/usrsctplib
	${libjuice_path}/include
	${libjuice_path}/src
	${libdatachannel_path}/src
	${libdatachannel_path}/include/rtc
	${boringssl_path}/include
)
target_link_libraries(datachannel
	srtp2
	usrsctp
	plog
	LibJuice::LibJuiceStatic
	nlohmann_json
	Threads::Threads
	ssl
	crypto
)
set_target_properties(datachannel PROPERTIES
	CXX_STANDARD 17
)
target_compile_features(datachannel PUBLIC cxx_std_17)
target_compile_definitions(datachannel PRIVATE
	RTC_ENABLE_MEDIA
	RTC_ENABLE_WEBSOCKET
	USE_GNUTLS=0
	USE_NICE=0
)

if(WIN32)
	target_link_libraries(datachannel PUBLIC ws2_32) # winsock2
endif()
