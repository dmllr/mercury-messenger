#include <cstring>
#include <jni.h>

#include "logging/logging.h"

#include "camera/camera_codec.hpp"
#include "camera/camera_manager.hpp"
#include "core/app_engine.hpp"


/**
 * AppEngine object:
 *   the top level application engine object
 */
AppEngine* engine = nullptr;


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
Java_me_hela_surveillance_CameraService_createAppEngine(
    JNIEnv* env,
    jobject instance,
    jint width, jint height) {
  engine = new AppEngine(env, instance, width, height);
  return reinterpret_cast<jlong>(engine);
}

/**
 * deleteAppEngine():
 *   releases native application object, which
 *   triggers native camera object be released
 */
extern "C" JNIEXPORT void JNICALL
Java_me_hela_surveillance_CameraService_deleteAppEngine(
    JNIEnv* env,
    jobject instance,
    jlong appmgr) {
  if (!engine || !appmgr)
    return;
  auto app = reinterpret_cast<AppEngine*>(appmgr);
  LOGF_IF(app != engine) << "NdkCamera Obj mismatch";

  delete app;

  // also reset the private global object
  engine = nullptr;
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
Java_me_hela_surveillance_CameraService_getMinimumCompatiblePreviewSize(
    JNIEnv* env,
    jobject instance,
    jlong appmgr) {
  if (!appmgr)
    return nullptr;

  auto app = reinterpret_cast<AppEngine*>(appmgr);
  jclass cls = env->FindClass("android/util/Size");
  jobject previewSize = env->NewObject(
      cls,
      env->GetMethodID(cls, "<init>", "(II)V"),
      app->getCompatibleCameraRes().width,
      app->getCompatibleCameraRes().height);

  return previewSize;
}

/**
 * getCameraSensorOrientation()
 * @ return camera sensor orientation angle relative to Android device's
 * display orientation. This sample only deal to back facing camera.
 */
extern "C" JNIEXPORT jint JNICALL
Java_me_hela_surveillance_CameraService_getCameraSensorOrientation(
    JNIEnv* env,
    jobject instance,
    jlong appmgr) {
  auto app = reinterpret_cast<AppEngine*>(appmgr);
  return app->getCameraSensorOrientation(ACAMERA_LENS_FACING_BACK);
}

extern "C" JNIEXPORT void JNICALL
Java_me_hela_surveillance_CameraService_requestPreview(
    JNIEnv* env,
    jobject instance,
    jlong appmgr,
    jboolean start) {
  auto app = reinterpret_cast<AppEngine*>(appmgr);
  app->requestPreview(start);
}

extern "C" JNIEXPORT void JNICALL
Java_me_hela_surveillance_CameraService_onPreviewSurfaceCreated(
    JNIEnv* env,
    jobject instance,
    jlong appmgr,
    jobject surface) {
  auto app = reinterpret_cast<AppEngine*>(appmgr);
  app->addPreviewSurface(surface);
}

extern "C" JNIEXPORT void JNICALL
Java_me_hela_surveillance_CameraService_onPreviewSurfaceDestroyed(
    JNIEnv* env,
    jobject instance,
    jlong appmgr,
    jobject surface) {
  auto app = reinterpret_cast<AppEngine*>(appmgr);

  app->removePreviewSurface();

  auto cls = env->FindClass("android/view/Surface");
  auto toString = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");

  auto destroyObjStr = reinterpret_cast<jstring>(env->CallObjectMethod(surface, toString));
  auto destroyObjName = env->GetStringUTFChars(destroyObjStr, nullptr);

  auto appObjStr = reinterpret_cast<jstring>(env->CallObjectMethod(app->getSurfaceObject(), toString));
  auto appObjName = env->GetStringUTFChars(appObjStr, nullptr);

  LOGF_IF(strcmp(destroyObjName, appObjName)) << "object name mismatch";

  env->ReleaseStringUTFChars(destroyObjStr, destroyObjName);
  env->ReleaseStringUTFChars(appObjStr, appObjName);
}

extern "C" JNIEXPORT void JNICALL
Java_me_hela_surveillance_CameraService_requestRtc(
    JNIEnv* env,
    jobject instance,
    jlong appmgr) {
  const auto& app = reinterpret_cast<AppEngine*>(appmgr);
  app->requestRtc();
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  // Grab the context ClassLoader of the current thread, if any.
  JNIEnv* env;
  if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnLoad(JavaVM* jvm, void* reserved) {
  JNIEnv* env;
  if (jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return;
  }
}
