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


extern "C" JNIEXPORT jlong JNICALL
Java_me_hela_surveillance_CameraService_createAppEngine(
    JNIEnv* env,
    jobject instance,
    jobject statusUpdateCallback) {
  engine = new AppEngine(env, instance, statusUpdateCallback);
  return reinterpret_cast<jlong>(engine);
}

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
