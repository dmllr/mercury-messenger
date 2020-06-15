#pragma once

#include <string>
#include <thread>
#include <functional>
#include <map>

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#include <media/NdkMediaCodec.h>

#include "camera-framed-source.hpp"
#include "camera-media-subsession.hpp"

#include "logging/logging.h"
#include "utils/likely.h"


namespace server {

class StreamingServer {
	std::thread server_thread;

	UsageEnvironment* env;
	RTSPServer* rtsp_server;
	std::map<unsigned, void*> active_sessions;

public:
	std::function<void()> onSessionStart;
	std::function<void()> onSessionStop;

	void startEventLoop() {
		server_thread = std::thread {[this]() {
			env->taskScheduler().doEventLoop(); // never returns
		}};
		server_thread.detach();
		Log::i("server_thread ready");
	}

	~StreamingServer() {
		shutdown();
	}

	void setupRTSPServer(AMediaCodec* codec) {
		TaskScheduler* scheduler = BasicTaskScheduler::createNew();
		env = BasicUsageEnvironment::createNew(*scheduler);

		UserAuthenticationDatabase* authDB = NULL;
//		authDB = new UserAuthenticationDatabase; // TODO auth? need?
//		authDB->addUserRecord("username1", "password1");

		rtsp_server = RTSPServer::createNew(*env, 8554, authDB);  // TODO port
		Log::assert_or_die(rtsp_server != NULL, "Failed to create RTSP server: %s", env->getResultMsg());

		const auto descriptionString = "Camera stream";
		const auto streamName = "camera";
		ServerMediaSession* sms = ServerMediaSession::createNew(*env, streamName, streamName, descriptionString);
		sms->addSubsession(CameraMediaSubsession::createNew(*env, codec, [this](unsigned clientSessionId, void* token, CameraMediaSubsession::Command command) {
			switch (command) {
				case CameraMediaSubsession::START:
					active_sessions[clientSessionId] = token;
					if (likely(onSessionStart))
						onSessionStart();
					break;
				case CameraMediaSubsession::STOP:
					active_sessions.erase(clientSessionId);
					if (likely(onSessionStop))
						onSessionStop();
					break;
			}
		}));
		rtsp_server->addServerMediaSession(sms);

		char* url = rtsp_server->rtspURL(sms);
		Log::i("Published RTSP stream as the URL '%s'", rtsp_server->rtspURL(sms));
		delete[] url;

		startEventLoop();
	}

	void start(AMediaCodec* codec) {
		setupRTSPServer(codec);
	}

	void shutdown() {
		Medium::close(rtsp_server);
	}

	bool hasActiveSessions() {
		return !active_sessions.empty();
	}

	std::map<unsigned int, void*> getActiveSessions() {
		return active_sessions;
	}
};

}  // namespace server
