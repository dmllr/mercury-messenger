#pragma once

#include <string>
#include <thread>
#include <functional>
#include <map>

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#include <media/NdkMediaCodec.h>

#include "camera_framed_source.hpp"
#include "camera_media_subsession.hpp"

#include "logging/logging.h"
#include "utils/likely.h"


namespace server {

class RtspManager {
	static constexpr auto target = CaptureTarget::RTSP_ENCODER_SURFACE;

	UsageEnvironment* env;
	RTSPServer* rtsp_server;
	std::map<unsigned, void*> active_sessions;

	std::thread server_thread;

protected:
	std::shared_ptr<CameraManager> camera_manager;

public:
	void startEventLoop() {
		server_thread = std::thread {[this]() {
			env->taskScheduler().doEventLoop(); // never returns
		}};
		server_thread.detach();
		LOGI << "server_thread ready";
	}

	RtspManager(std::shared_ptr<CameraManager> camera_manager) : camera_manager(camera_manager) {
	}

	~RtspManager() {
		shutdown();
	}

	void setupRTSPServer(AMediaCodec* codec) {
		TaskScheduler* scheduler = BasicTaskScheduler::createNew();
		env = BasicUsageEnvironment::createNew(*scheduler);

		UserAuthenticationDatabase* authDB = NULL;
//		authDB = new UserAuthenticationDatabase; // TODO auth? need?
//		authDB->addUserRecord("username1", "password1");

		rtsp_server = RTSPServer::createNew(*env, 8554, authDB);  // TODO port
		LOGF_IF(rtsp_server == NULL) << "Failed to create RTSP server: " << env->getResultMsg();

		const auto descriptionString = "Camera stream";
		const auto streamName = "camera";
		ServerMediaSession* sms = ServerMediaSession::createNew(*env, streamName, streamName, descriptionString);
		sms->addSubsession(CameraMediaSubsession::createNew(*env, codec, [this](unsigned clientSessionId, void* token, CameraMediaSubsession::Command command) {
			switch (command) {
				case CameraMediaSubsession::START:
					active_sessions[clientSessionId] = token;
					camera_manager->addConsumer(target);
					break;
				case CameraMediaSubsession::STOP:
					active_sessions.erase(clientSessionId);
					camera_manager->removeConsumer(target);
					break;
			}
		}));
		rtsp_server->addServerMediaSession(sms);

		char* url = rtsp_server->rtspURL(sms);
		LOGI << "Published RTSP stream as the URL '" << rtsp_server->rtspURL(sms) << "'";
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
