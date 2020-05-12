#include <jni.h>
#include <cstring>
#include <logging/logging.h>
#include "camera/camera_codec.hpp"
#include "camera/camera_manager.hpp"
#include "camera/app_engine.hpp"

#include <sdk/android/native_api/base/init.h>
#include <webrtc/modules/utility/include/jvm_android.h>
#include <webrtc/rtc_base/ssl_adapter.h>


/**
 * Application object:
 *   the top level camera application object
 */
AppEngine* cameraEngine = nullptr;

/**
 * createCamera() Create application instance and NDK camera object
 * @param  width is the texture view window width
 * @param  height is the texture view window height
 * In this sample, it takes the simplest approach in that:
 * the android system display size is used to view full screen
 * preview camera images. Onboard Camera most likely to
 * support the onboard panel size on that device. Camera is most likely
 * to be oriented as landscape mode, the input width/height is in
 * landscape mode. TextureView on Java side handles rotation for
 * portrait mode.
 * @return application object instance ( not used in this sample )
 */
extern "C" JNIEXPORT jlong JNICALL
Java_me_hela_surveillance_camera_CameraService_createCameraEngine(
		JNIEnv* env,
		jobject instance,
		jint width, jint height) {
	cameraEngine = new AppEngine(env, instance, width, height);
	return reinterpret_cast<jlong>(cameraEngine);
}

/**
 * deleteCamera():
 *   releases native application object, which
 *   triggers native camera object be released
 */
extern "C" JNIEXPORT void JNICALL
Java_me_hela_surveillance_camera_CameraService_deleteCameraEngine(
		JNIEnv* env,
		jobject instance,
		jlong ndkCameraObj) {
	if (!cameraEngine || !ndkCameraObj)
		return;
	auto app = reinterpret_cast<AppEngine*>(ndkCameraObj);
	Log::assert_or_die(app == cameraEngine, "NdkCamera Obj mismatch");

	delete app;

	// also reset the private global object
	cameraEngine = nullptr;
}

/**s
 * getCameraCompatibleSize()
 * @returns minimium camera preview window size for the given
 * requested camera size in CreateCamera() function, with the same
 * ascpect ratio. essentially,
 *   1) Display device size decides NDKCamera object preview size
 *   2) Chosen NDKCamera preview size passed to TextView to
 *      reset textureView size
 *   3) textureView size is stretched when previewing image
 *      on display device
 */
extern "C" JNIEXPORT jobject JNICALL
Java_me_hela_surveillance_camera_CameraService_getMinimumCompatiblePreviewSize(
		JNIEnv* env,
		jobject instance,
		jlong ndkCameraObj) {
	if (!ndkCameraObj)
		return nullptr;

	auto app = reinterpret_cast<AppEngine*>(ndkCameraObj);
	jclass cls = env->FindClass("android/util/Size");
	jobject previewSize = env->NewObject(
		cls,
		env->GetMethodID(cls, "<init>", "(II)V"),
		app->getCompatibleCameraRes().width,
		app->getCompatibleCameraRes().height
	);

	return previewSize;
}

/**
 * getCameraSensorOrientation()
 * @ return camera sensor orientation angle relative to Android device's
 * display orientation. This sample only deal to back facing camera.
 */
extern "C" JNIEXPORT jint JNICALL
Java_me_hela_surveillance_camera_CameraService_getCameraSensorOrientation(
		JNIEnv* env,
		jobject instance,
		jlong ndkCameraObj) {
	Log::assert_or_die(ndkCameraObj, "NativeObject should not be null Pointer");
	auto app = reinterpret_cast<AppEngine*>(ndkCameraObj);
	return app->getCameraSensorOrientation(ACAMERA_LENS_FACING_BACK);
}

extern "C" JNIEXPORT void JNICALL
Java_me_hela_surveillance_camera_CameraService_requestPreview(
		JNIEnv* env,
		jobject instance,
		jlong ndkCameraObj,
		jboolean start) {
	Log::assert_or_die(ndkCameraObj && (jlong) cameraEngine == ndkCameraObj, "ndkCameraObj should not be null");

	auto app = reinterpret_cast<AppEngine*>(ndkCameraObj);
	app->requestPreview(start);
}

extern "C" JNIEXPORT void JNICALL
Java_me_hela_surveillance_camera_CameraService_onPreviewSurfaceCreated(
		JNIEnv* env,
		jobject instance,
		jlong ndkCameraObj,
		jobject surface) {
	Log::assert_or_die(ndkCameraObj && (jlong) cameraEngine == ndkCameraObj, "ndkCameraObj should not be null");

	auto app = reinterpret_cast<AppEngine*>(ndkCameraObj);
	app->addPreviewSurface(surface);
}

extern "C" JNIEXPORT void JNICALL
Java_me_hela_surveillance_camera_CameraService_onPreviewSurfaceDestroyed(
		JNIEnv* env,
		jobject instance,
		jlong ndkCameraObj,
		jobject surface) {
	auto app = reinterpret_cast<AppEngine*>(ndkCameraObj);

	app->removePreviewSurface();

	Log::assert_or_die(ndkCameraObj && cameraEngine == app, "NativeObject should not be null Pointer");

	jclass cls = env->FindClass("android/view/Surface");
	jmethodID toString = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");

	auto destroyObjStr = reinterpret_cast<jstring>(env->CallObjectMethod(surface, toString));
	const char* destroyObjName = env->GetStringUTFChars(destroyObjStr, nullptr);

	auto appObjStr = reinterpret_cast<jstring>(env->CallObjectMethod(app->getSurfaceObject(), toString));
	const char* appObjName = env->GetStringUTFChars(appObjStr, nullptr);

	Log::assert_or_die(!strcmp(destroyObjName, appObjName), "object Name MisMatch");

	env->ReleaseStringUTFChars(destroyObjStr, destroyObjName);
	env->ReleaseStringUTFChars(appObjStr, appObjName);
}
