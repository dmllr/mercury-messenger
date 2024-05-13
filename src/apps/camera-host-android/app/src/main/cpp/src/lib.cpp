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
Java_hela_eillance_CameraService_createAppEngine__Lhela_eillance_events_IStatusUpdateCallback_2(
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

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
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
