#include <cstring>
#include <jni.h>

#include "hela/eillance/logging/logging.h"

#include "app_engine.hpp"


/**
 * AppEngine object:
 *   the top level application engine object
 */
using AppEngine = hela::eillance::AppEngine;
AppEngine* engine = nullptr;

JavaVM* g_JavaVM;

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_CameraService_createAppEngine(
    JNIEnv* env,
    jobject instance,
    jobject statusUpdateCallback) {
  engine = new AppEngine(g_JavaVM, env, instance, statusUpdateCallback);
  return reinterpret_cast<jlong>(engine);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CameraService_deleteAppEngine(
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

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CameraService_requestPreview(
    JNIEnv* env,
    jobject instance,
    jlong appmgr,
    jboolean start) {
  auto app = reinterpret_cast<AppEngine*>(appmgr);
  app->requestPreview(start);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CameraService_onPreviewSurfaceCreated(
    JNIEnv* env,
    jobject instance,
    jlong appmgr,
    jobject surface) {
  auto app = reinterpret_cast<AppEngine*>(appmgr);
  app->addPreviewSurface(surface);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CameraService_onPreviewSurfaceDestroyed(
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

  auto appObjStr = reinterpret_cast<jstring>(env->CallObjectMethod(app->getPreviewSurface(), toString));
  auto appObjName = env->GetStringUTFChars(appObjStr, nullptr);

  LOGF_IF(strcmp(destroyObjName, appObjName)) << "object name mismatch";

  env->ReleaseStringUTFChars(destroyObjStr, destroyObjName);
  env->ReleaseStringUTFChars(appObjStr, appObjName);
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  // Grab the context ClassLoader of the current thread, if any.
  JNIEnv* env;
  if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }
  g_JavaVM = vm;
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnLoad(JavaVM* jvm, void* reserved) {
  JNIEnv* env;
  if (jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return;
  }
}
