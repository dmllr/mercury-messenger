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

/**
 * CameraAppEngine
 */
class AppEngine {
	JNIEnv* env;
	int32_t request_width;
	int32_t request_height;
	jobject preview_surface = nullptr;
	CameraManager* camera_manager = nullptr;
	CameraCodec* camera_codec = nullptr;
	server::StreamingServer* streaming_server = nullptr;
	ImageFormat compatible_camera_res;

public:
	explicit AppEngine(JNIEnv* env, jobject instance, jint w, jint h)
				: env(env),
				  request_width(w),
				  request_height(h) {
		memset(&compatible_camera_res, 0, sizeof(compatible_camera_res));

		camera_codec = new CameraCodec();

		camera_manager = new CameraManager();
		camera_manager->matchCaptureSizeRequest(request_width, request_height, &compatible_camera_res);

		streaming_server = new server::StreamingServer();
		streaming_server->start(camera_codec->getCodec());
		streaming_server->onSessionStart = [this]() {
			camera_codec->start();
			if (streaming_server->getActiveSessions().size() == 1)
				camera_manager->addCaptureRequestTarget(CaptureTarget::ENCODER_SURFACE_IDX, camera_codec->getSurface());
			camera_manager->setStreamRequested(streaming_server->hasActiveSessions());
		};
		streaming_server->onSessionStop = [this]() {
			camera_manager->setStreamRequested(streaming_server->hasActiveSessions());
			if (!streaming_server->hasActiveSessions())
				camera_manager->removeCaptureRequestTarget(CaptureTarget::ENCODER_SURFACE_IDX);
			camera_codec->stop();
		};
	}

	~AppEngine() {
		if (camera_manager) {
			delete camera_manager;
			camera_manager = nullptr;
		}

		if (streaming_server) {
			delete streaming_server;
			streaming_server = nullptr;
		}

		if (camera_codec) {
			delete camera_codec;
			camera_codec = nullptr;
		}

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
};
