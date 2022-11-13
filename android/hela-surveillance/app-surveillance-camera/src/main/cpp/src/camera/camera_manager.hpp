#pragma once

#include <string>
#include <vector>
#include <map>
#include <utility>
#include <queue>
#include <cinttypes>
#include <cstring>

#include <jni.h>
#include <media/NdkImage.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraMetadataTags.h>
#include <__mutex_base>
#include <mutex>

#include "camera_codec.hpp"
#include "camera_manager.hpp"

#include "camera/camera_utils.hpp"
#include "logging/logging.h"


enum class CaptureSessionState : int32_t {
	READY = 0,  // session is ready
	ACTIVE,     // session is busy
	CLOSED,     // session is closed by itself or a new session evicts
};

enum CaptureTarget {
	PREVIEW_SURFACE = 0,
	RTSP_ENCODER_SURFACE,
	RTC_ENCODER_SURFACE,
};

struct CameraRequest {
	size_t n_consumers;
	ANativeWindow* output_window;
	ACaptureSessionOutput* session_output;
	ACameraOutputTarget* target;
	ACaptureRequest* capture_request;
};

struct ImageFormat {
	int32_t width;
	int32_t height;

	int32_t format;
};


class DisplayDimension {
private:
	int32_t width, height;
	bool is_portrait;

public:
	DisplayDimension(int32_t w, int32_t h) : width(w), height(h), is_portrait(false) {
		if (h > w) {
			// make it landscape
			width = h;
			height = w;
			is_portrait = true;
		}
	}

	DisplayDimension(const DisplayDimension& other) {
		width = other.width;
		height = other.height;
		is_portrait = other.is_portrait;
	}

	DisplayDimension(void) {
		width = 0;
		height = 0;
		is_portrait = false;
	}

	DisplayDimension& operator=(const DisplayDimension& other) {
		width = other.width;
		height = other.height;
		is_portrait = other.is_portrait;

		return (*this);
	}

	bool operator>(DisplayDimension& other) {
		return (width >= other.width & height >= other.height);
	}

	bool operator==(DisplayDimension& other) {
		return (width == other.width && height == other.height && is_portrait == other.is_portrait);
	}

	DisplayDimension operator-(DisplayDimension& other) {
		DisplayDimension delta(width - other.width, height - other.height);
		return delta;
	}

	bool isSameRatio(DisplayDimension& other) {
		return (width * other.height == height * other.width);
	}

	void flip(void) {
		is_portrait = !is_portrait;
	}

	bool isPortrait(void) {
		return is_portrait;
	}

	int32_t orgWidth(void) {
		return (is_portrait ? height : width);
	}

	int32_t orgHeight(void) {
		return (is_portrait ? width : height);
	}
};


class CameraId {
public:
	std::string id;
	ACameraDevice* device;
	acamera_metadata_enum_android_lens_facing_t facing;
	bool available;  // free to use
	bool owner;      // we are the owner of the camera

	explicit CameraId(const char* id)
		: id(id),
		  device(nullptr),
		  facing(ACAMERA_LENS_FACING_FRONT),
		  available(false),
		  owner(false) {
	}

	explicit CameraId() {
		CameraId("");
	}
};


class CameraManager {

private:
	ACameraManager* camera_manager;
	std::map<std::string, CameraId> cameras;
	std::string active_camera_id;
	int32_t camera_facing;
	int32_t camera_orientation;

	std::map<CaptureTarget, CameraRequest> camera_requests;

	ACaptureSessionOutputContainer* output_container;
	ACameraCaptureSession* capture_session;
	CaptureSessionState capture_session_state;

	std::shared_ptr<CameraCodec> camera_codec = nullptr;

	std::condition_variable cv;
	std::mutex mutex;

public:
	CameraManager() :
		camera_manager(nullptr),
		active_camera_id(""),
		camera_facing(ACAMERA_LENS_FACING_BACK),
		camera_orientation(0),
		camera_codec(std::make_shared<CameraCodec>(CameraCodec())),
		output_container(nullptr),
		capture_session(nullptr),
		capture_session_state(CaptureSessionState::CLOSED)
	 {
		camera_manager = ACameraManager_create();
		Log::assert_or_die(camera_manager != nullptr, "Failed to create camera_manager");

		// Pick up a back-facing camera to preview
		 enumerateCameras();
		Log::assert_or_die(active_camera_id.size() != 0, "Unknown ActiveCameraIdx");
	}

	~CameraManager() {
		stopCapture();
		ACameraCaptureSession_close(capture_session);

		for (auto& it : camera_requests)
			invalidateCaptureRequest(it.first);

		camera_requests.clear();

		ACaptureSessionOutputContainer_free(output_container);

		for (auto& cam : cameras) {
			if (cam.second.device) {
				auto status = ACameraDevice_close(cam.second.device);
				Log::assert_soft(status == ACAMERA_OK, "ACameraDevice_close call failed with code: %#x, %s", status, GetErrorStr(status));
			}
		}
		cameras.clear();

		if (camera_manager) {
			auto status = ACameraManager_unregisterAvailabilityCallback(camera_manager, GetManagerListener());
			Log::assert_soft(status == ACAMERA_OK, "ACameraManager_unregisterAvailabilityCallback call failed with code: %#x, %s", status, GetErrorStr(status));
			ACameraManager_delete(camera_manager);
			camera_manager = nullptr;
		}
	}

	/**
	 * enumerateCameras()
	 *     Loop through cameras on the system, pick up
	 *     1) back facing one if available
	 *     2) otherwise pick the first one reported to us
	 */
	void enumerateCameras() {
		cameras.clear();

		ACameraIdList* cameraIds = nullptr;
		auto status = ACameraManager_getCameraIdList(camera_manager, &cameraIds);
		Log::assert_or_die(status == ACAMERA_OK, "ACameraManager_getCameraIdList call failed with code: %#x, %s", status, GetErrorStr(status));

		for (int i = 0; i < cameraIds->numCameras; ++i) {
			const char* id = cameraIds->cameraIds[i];

			ACameraMetadata* metadataObj;
			status = ACameraManager_getCameraCharacteristics(camera_manager, id, &metadataObj);
			Log::assert_or_die(status == ACAMERA_OK, "ACameraManager_getCameraCharacteristics call failed with code: %#x, %s", status, GetErrorStr(status));

			int32_t count = 0;
			const uint32_t* tags = nullptr;
			ACameraMetadata_getAllTags(metadataObj, &count, &tags);
			for (int tagIdx = 0; tagIdx < count; ++tagIdx) {
				if (ACAMERA_LENS_FACING == tags[tagIdx]) {
					ACameraMetadata_const_entry lensInfo = {
						0,
					};
					status = ACameraMetadata_getConstEntry(metadataObj, tags[tagIdx], &lensInfo);
					Log::assert_soft(status == ACAMERA_OK, "ACameraMetadata_getConstEntry call failed with code: %#x, %s", status, GetErrorStr(status));
					CameraId cam(id);
					cam.facing = static_cast<acamera_metadata_enum_android_lens_facing_t>(lensInfo.data.u8[0]);
					cam.owner = false;
					cam.device = nullptr;
					cameras[cam.id] = cam;
					if (cam.facing == ACAMERA_LENS_FACING_BACK)
						active_camera_id = cam.id;
					break;
				}
			}
			ACameraMetadata_free(metadataObj);
		}

		Log::assert_or_die(!cameras.empty(), "No Camera Available on the device");
		if (active_camera_id.length() == 0) {
			// if no dedicated facing camera found, pick up the first one
			active_camera_id = cameras.begin()->second.id;
		}
		ACameraManager_deleteCameraIdList(cameraIds);
	}

	void renewCaptureSession() {
		if (capture_session_state == CaptureSessionState::ACTIVE) {
			stopCapture();

			std::unique_lock<std::mutex> lk(mutex);
			cv.wait(lk);
		}

		auto active_requests = false;
		for (auto& it : camera_requests)
			active_requests |= it.second.n_consumers > 0;

		if (!active_requests)
			return;

		auto status = ACameraManager_openCamera(camera_manager, active_camera_id.c_str(), GetDeviceListener(), &cameras[active_camera_id].device);
		Log::assert_or_die(status == ACAMERA_OK, "ACameraManager_openCamera call failed with code: %#x, %s", status, GetErrorStr(status));

		status = ACameraManager_registerAvailabilityCallback(camera_manager, GetManagerListener());
		Log::assert_or_die(status == ACAMERA_OK, "ACameraManager_registerAvailabilityCallback call failed with code: %#x, %s", status, GetErrorStr(status));

		status = ACaptureSessionOutputContainer_create(&output_container);
		Log::assert_or_die(status == ACAMERA_OK, "ACaptureSessionOutputContainer_create call failed with code: %#x, %s", status, GetErrorStr(status));

		for (auto& it : camera_requests) {
			auto& req = it.second;

			if (req.n_consumers <= 0) {
				continue;
			}

			ANativeWindow_acquire(req.output_window);

			status = ACaptureSessionOutput_create(req.output_window, &req.session_output);
			Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

			status = ACaptureSessionOutputContainer_add(output_container, req.session_output);
			Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

			status = ACameraOutputTarget_create(req.output_window, &req.target);
			Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

			status = ACameraDevice_createCaptureRequest(cameras[active_camera_id].device, TEMPLATE_RECORD, &req.capture_request);
			Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

			status = ACaptureRequest_addTarget(req.capture_request, req.target);
			Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));
		}

		// Create a capture session for the given preview requests
		capture_session_state = CaptureSessionState::READY;
		status = ACameraDevice_createCaptureSession(
			cameras[active_camera_id].device,
			output_container,
			GetSessionListener(),
			&capture_session
		);
		Log::assert_or_die(status == ACAMERA_OK, "ACameraDevice_createCaptureSession failed with status: %#x, %s", status, GetErrorStr(status));

		startCapture();
	}

	std::shared_ptr<CameraCodec> codec() {
		return camera_codec;
	}

	void startCapture() {
		auto requests{std::vector<ACaptureRequest*>()};
		for (auto& it : camera_requests) {
			auto& req = it.second;
			if (req.n_consumers > 0)
				requests.emplace_back(req.capture_request);
		}

		if (likely(!requests.empty())) {
			Log::i("For the camera Capture Session requested Repeating Request for %d requests", requests.size());
			auto status = ACameraCaptureSession_setRepeatingRequest(
				capture_session, nullptr,
				requests.size(), std::move(requests.data()),
				nullptr
			);
			Log::assert_or_die(status == ACAMERA_OK, "ACameraCaptureSession_setRepeatingRequest call failed with code: %#x, %s", status, GetErrorStr(status));
			Log::i("ACameraCaptureSession_setRepeatingRequest done");
		}
	}

	void stopCapture() {
		if (capture_session_state == CaptureSessionState::ACTIVE)
			ACameraCaptureSession_stopRepeating(capture_session);
	}

	void invalidateCaptureRequest(const CaptureTarget target) {
		auto& req = camera_requests[target];

		if (req.target) {
			auto status = ACaptureRequest_removeTarget(req.capture_request, req.target);
			Log::assert_or_die(status == ACAMERA_OK,"ACaptureRequest_removeTarget call failed with code: %#x, %s", status, GetErrorStr(status));
			ACameraOutputTarget_free(req.target);
			req.target = nullptr;
		}

		if (req.session_output) {
			auto status = ACaptureSessionOutputContainer_remove(output_container, req.session_output);
			Log::assert_or_die(status == ACAMERA_OK, "ACaptureSessionOutputContainer_remove call failed with code: %#x, %s", status, GetErrorStr(status));
			ACaptureSessionOutput_free(req.session_output);
			req.session_output = nullptr;
		}

		if (req.capture_request) {
			ACaptureRequest_free(req.capture_request);
			req.capture_request = nullptr;
			ANativeWindow_release(req.output_window);
		}
	}

	void registerSurfaceForTarget(CaptureTarget target, ANativeWindow* window) {
		if (camera_requests.find(target) == camera_requests.end())
			camera_requests[target] = CameraRequest();

		camera_requests[target].output_window = window;
	}

	void removeCaptureRequestTarget(CaptureTarget target) {
		camera_requests.erase(target);
	}

	void addConsumer(CaptureTarget target) {
		if (camera_requests[target].n_consumers == 0) {
			// first consumer added
			if (target == CaptureTarget::RTC_ENCODER_SURFACE || target == CaptureTarget::RTSP_ENCODER_SURFACE) {
				registerSurfaceForTarget(target, camera_codec->getSurface());
			}
		}
		camera_requests[target].n_consumers++;
		renewCaptureSession();
	}

	void removeConsumer(CaptureTarget target) {
		camera_requests[target].n_consumers--;
		Log::assert_or_die(camera_requests[target].n_consumers >= 0, "Number of consumers for target %d is below 0", target);
		if (camera_requests[target].n_consumers == 0) {
			// no more consumers
			removeCaptureRequestTarget(target);
		}
		renewCaptureSession();
	}

	/**
	 * GetSensorOrientation()
	 *     Retrieve current sensor orientation regarding to the phone device orientation
	 */
	bool getSensorOrientation(int32_t* facing, int32_t* angle) {
		if (!camera_manager)
			return false;

		ACameraMetadata* metadataObj;
		ACameraMetadata_const_entry face, orientation;
		auto status = ACameraManager_getCameraCharacteristics(camera_manager, active_camera_id.c_str(), &metadataObj);
		Log::assert_or_die(status == ACAMERA_OK, "ACameraManager_getCameraCharacteristics call failed with code: %#x, %s", status, GetErrorStr(status));
		status = ACameraMetadata_getConstEntry(metadataObj, ACAMERA_LENS_FACING, &face);
		Log::assert_or_die(status == ACAMERA_OK, "ACameraMetadata_getConstEntry call failed with code: %#x, %s", status, GetErrorStr(status));
		camera_facing = static_cast<int32_t>(face.data.u8[0]);

		status = ACameraMetadata_getConstEntry(metadataObj, ACAMERA_SENSOR_ORIENTATION, &orientation);
		Log::assert_or_die(status == ACAMERA_OK, "ACameraMetadata_getConstEntry call failed with code: %#x, %s", status, GetErrorStr(status));

		Log::i("====Current SENSOR_ORIENTATION: %8d", orientation.data.i32[0]);

		ACameraMetadata_free(metadataObj);
		camera_orientation = orientation.data.i32[0];

		if (facing)
			*facing = camera_facing;
		if (angle)
			*angle = camera_orientation;

		return true;
	}

	bool matchCaptureSizeRequest(
		int32_t requestWidth,
		int32_t requestHeight,
		ImageFormat* resView
	) {
		DisplayDimension disp(requestWidth, requestHeight);
		if (camera_orientation == 90 || camera_orientation == 270)
			disp.flip();

		ACameraMetadata* metadata;
		auto status = ACameraManager_getCameraCharacteristics(camera_manager, active_camera_id.c_str(), &metadata);
		Log::assert_or_die(status == ACAMERA_OK, "ACameraManager_getCameraCharacteristics call failed with code: %#x, %s", status, GetErrorStr(status));

		ACameraMetadata_const_entry entry;
		status = ACameraMetadata_getConstEntry(metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
		Log::assert_or_die(status == ACAMERA_OK, "ACameraMetadata_getConstEntry call failed with code: %#x, %s", status, GetErrorStr(status));

		// format of the data: format, width, height, input?, type int32
		bool foundIt = false;
		DisplayDimension foundRes(1280, 720);

		for (int i = 0; i < entry.count; i += 4) {
			int32_t input = entry.data.i32[i + 3];
			int32_t format = entry.data.i32[i + 0];
			if (input) continue;

			if (format == AIMAGE_FORMAT_YUV_420_888 || format == AIMAGE_FORMAT_JPEG) {
				DisplayDimension res(entry.data.i32[i + 1], entry.data.i32[i + 2]);
				if (!disp.isSameRatio(res))
					continue;
				if (format == AIMAGE_FORMAT_YUV_420_888 && foundRes > res) {
					foundIt = true;
					foundRes = res;
				}
			}
		}

		if (foundIt) {
			resView->width = foundRes.orgWidth();
			resView->height = foundRes.orgHeight();
		} else {
			Log::w("Did not find any compatible camera resolution, taking 1280x720");
			if (disp.isPortrait()) {
				resView->width = 720;
				resView->height = 1280;
			} else {
				resView->width = 1280;
				resView->height = 720;
			}
		}

		resView->format = AIMAGE_FORMAT_YUV_420_888;

		return foundIt;
	}

private:
	void finishClosing() {
		ACameraCaptureSession_close(capture_session);

		for (auto& it : camera_requests)
			invalidateCaptureRequest(it.first);

		auto status = ACameraDevice_close(cameras[active_camera_id].device);
		Log::assert_soft(status == ACAMERA_OK, "ACameraDevice_close call for active camera failed with code: %#x, %s", status, GetErrorStr(status));

		status = ACameraManager_unregisterAvailabilityCallback(camera_manager, GetManagerListener());
		Log::assert_soft(status == ACAMERA_OK, "ACameraManager_unregisterAvailabilityCallback call failed with code: %#x, %s", status, GetErrorStr(status));

		{
			std::lock_guard<std::mutex> lk(mutex);
			Log::i("Streaming stopped");
		}
		cv.notify_all();
	}

	/**
	 * Handles capture session state changes.
	 *   Update into internal session state.
	 */
	void onSessionState(ACameraCaptureSession* ses, CaptureSessionState state) {
		if (!ses || ses != capture_session) {
			Log::w("CaptureSession is %s", (ses ? "NOT our session" : "NULL"));
			return;
		}

		capture_session_state = state;
	}

	/**
	 * Availability Callbacks
	 * @return ACameraManager_AvailabilityCallback
	 */
	ACameraManager_AvailabilityCallbacks* GetManagerListener() {
		static ACameraManager_AvailabilityCallbacks cameraMgrListener = {
			.context = this,
			.onCameraAvailable = [](void* ctx, const char* id){
				reinterpret_cast<CameraManager*>(ctx)->onCameraStatusChanged(id, true);
			},
			.onCameraUnavailable = [](void* ctx, const char* id){
				reinterpret_cast<CameraManager*>(ctx)->onCameraStatusChanged(id, false);
			},
		};
		return &cameraMgrListener;
	}

	/**
	 * Device State Callbacks
	 * @return ACameraDevice_stateCallbacks
	 */
	ACameraDevice_stateCallbacks* GetDeviceListener() {
		static ACameraDevice_stateCallbacks cameraDeviceListener = {
			.context = this,
			.onDisconnected = [](void* ctx, ACameraDevice* dev) {
				reinterpret_cast<CameraManager*>(ctx)->onDeviceState(dev);
			},
			.onError = [](void* ctx, ACameraDevice* dev, int err) {
				reinterpret_cast<CameraManager*>(ctx)->onDeviceError(dev, err);
			},
		};
		return &cameraDeviceListener;
	}

	/**
	 * Capture Session Callbacks
	 * @return ACameraCaptureSession_stateCallbacks
	 */
	ACameraCaptureSession_stateCallbacks* GetSessionListener() {
		static ACameraCaptureSession_stateCallbacks sessionListener = {
			.context = this,
			.onClosed = [](void* ctx, ACameraCaptureSession* ses) {
				Log::w("Camera Capture Session %p closed", ses);
				reinterpret_cast<CameraManager*>(ctx)->onSessionState(ses, CaptureSessionState::CLOSED);
			},
			.onReady = [](void* ctx, ACameraCaptureSession* ses) {
				Log::w("Camera Capture Session %p ready", ses);
				auto ths = reinterpret_cast<CameraManager*>(ctx);
				ths->finishClosing();
				ths->onSessionState(ses, CaptureSessionState::READY);
			},
			.onActive = [](void* ctx, ACameraCaptureSession* ses) {
				Log::w("Camera Capture Session %p active", ses);
				reinterpret_cast<CameraManager*>(ctx)->onSessionState(ses, CaptureSessionState::ACTIVE);
			},
		};
		return &sessionListener;
	}

	void onCameraStatusChanged(const char* id, bool available) {
		cameras[std::string(id)].available = available;
	}

	/**
	 * Handle Camera DeviceStateChanges msg, notify device is disconnected
	 * simply close the camera
	 */
	void onDeviceState(ACameraDevice* dev) {
		std::string id(ACameraDevice_getId(dev));
		Log::w("Device %s is disconnected", id.c_str());

		cameras[id].available = false;
		ACameraDevice_close(cameras[id].device);
		cameras.erase(id);
	}

	/**
	 * Handles Camera's deviceErrorChanges message
	 */
	void onDeviceError(ACameraDevice* dev, int err) {
		std::string id(ACameraDevice_getId(dev));

		Log::e("CameraDevice %s is in error %#x", id.c_str(), err);
		Log::e("CameraDeviceError(%#x): %s", err, GetCameraDeviceErrorStr(err));

		CameraId& cam = cameras[id];

		switch (err) {
			case ERROR_CAMERA_IN_USE:
				cam.available = false;
				cam.owner = false;
				break;
			case ERROR_CAMERA_SERVICE:
			case ERROR_CAMERA_DEVICE:
			case ERROR_CAMERA_DISABLED:
			case ERROR_MAX_CAMERAS_IN_USE:
				cam.available = false;
				cam.owner = false;
				break;
			default:
				Log::i("Unknown Camera Device Error: %#x", err);
		}
	}

};
