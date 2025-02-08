#include "observer_engine.hpp"

namespace {

/// @brief Global JVM object
JavaVM* javaVm = nullptr;

std::unique_ptr<hela::eillance::ObserverEngine> observer{};

}  // namespace

/// @brief JNI function to initialize the decoder with a SurfaceTexture from Kotlin.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_initDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jobject native_bridge, jobject surface) {
  namespace he = hela::eillance;
  observer = std::make_unique<he::ObserverEngine>(javaVm, env);
  observer->SetTargetSurface(env, surface);
  observer->SetNativeBridge(env, native_bridge);
  observer->Start();
}

/// @brief JNI function to release decoder resources.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_releaseDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv*, jobject) {
  observer->Stop();
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  javaVm = vm;
  return JNI_VERSION_1_6;
}
