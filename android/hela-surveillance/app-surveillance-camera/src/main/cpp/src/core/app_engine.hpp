#pragma once

#include <thread>
#include <functional>

#include <jni.h>
#include <android/native_activity.h>
#include <android/native_window_jni.h>
#include <android/native_window.h>

#include "camera/camera_manager.hpp"
#include "camera/camera_codec.hpp"
#include "server/streaming-server.hpp"
#include "rtc/rtc_manager.hpp"
#include "rtc/audio_buffer_source.hpp"
#include "rtc/camerabuffersource.hpp"


/**
 * AppEngine
 */
class AppEngine {
	JNIEnv* env;
    jobject service_instance;
	int32_t request_width;
	int32_t request_height;
	jobject preview_surface = nullptr;
	std::shared_ptr<CameraManager> camera_manager;
	std::shared_ptr<RtcManager> rtc_manager;
	std::shared_ptr<server::StreamingServer> streaming_server;
	ImageFormat compatible_camera_res;

public:
	explicit AppEngine(JNIEnv* env, jobject instance, jint w, jint h)
				: env(env),
				  service_instance(env->NewWeakGlobalRef(instance)),
				  request_width(w),
				  request_height(h) {
		memset(&compatible_camera_res, 0, sizeof(compatible_camera_res));

		camera_manager = std::make_shared<CameraManager>();
		camera_manager->matchCaptureSizeRequest(request_width, request_height, &compatible_camera_res);

		rtc_manager = std::make_shared<RtcManager>();
		const auto stream = createRtcStream();
		rtc_manager->setStream(stream);
		rtc_manager->start();

		streaming_server = std::make_shared<server::StreamingServer>();
		streaming_server->start(camera_manager->codec()->getCodec());
		streaming_server->onSessionStart = [this]() {
			if (streaming_server->getActiveSessions().size() == 1)
				camera_manager->addCaptureRequestTarget(CaptureTarget::ENCODER_SURFACE_IDX, camera_manager->surface());
			camera_manager->setStreamRequested(streaming_server->hasActiveSessions());
		};
		streaming_server->onSessionStop = [this]() {
			camera_manager->setStreamRequested(streaming_server->hasActiveSessions());
			if (!streaming_server->hasActiveSessions())
				camera_manager->removeCaptureRequestTarget(CaptureTarget::ENCODER_SURFACE_IDX);
		};
	}

	~AppEngine() {
		if (preview_surface) {
			env->DeleteGlobalRef(preview_surface);
			preview_surface = nullptr;
		}
	}

	const ImageFormat& getCompatibleCameraRes() const {
		return compatible_camera_res;
	}

	int32_t getCameraSensorOrientation(int32_t requestFacing) {
		int32_t facing = 0, angle = 0;
		if (camera_manager->getSensorOrientation(&facing, &angle) ||
			facing == requestFacing) {
			return angle;
		}
		return 0;
	}

	jobject getSurfaceObject() {
		return preview_surface;
	}

	void addPreviewSurface(jobject surface) {
		if (preview_surface)
			env->DeleteGlobalRef(preview_surface);

		preview_surface = env->NewGlobalRef(surface);

		camera_manager->addCaptureRequestTarget(CaptureTarget::PREVIEW_SURFACE_IDX, ANativeWindow_fromSurface(env, surface));
	}

	void removePreviewSurface() {
		if (preview_surface) {
			env->DeleteGlobalRef(preview_surface);
			preview_surface = nullptr;
		}

		camera_manager->removeCaptureRequestTarget(CaptureTarget::PREVIEW_SURFACE_IDX);
	}

	void requestPreview(bool start) {
		camera_manager->setPreviewRequested(start);
	}

	void requestRtc() {
		rtc_manager->start();
	}


protected:

	std::shared_ptr<Stream> createRtcStream() {
		auto video = std::make_shared<CameraBufferSource>(camera_manager);
		auto audio = std::make_shared<AudioBufferSource>();

		return std::make_shared<Stream>(video, audio);
	}
};
