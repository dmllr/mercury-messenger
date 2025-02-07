#include "observer_engine.hpp"

namespace {

std::unique_ptr<hela::eillance::ObserverEngine> observer{};

}  // namespace

/// @brief JNI function to initialize the decoder with a SurfaceTexture from Kotlin.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_initDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jobject surface) {
  observer = std::make_unique<hela::eillance::ObserverEngine>();
  observer->SetTargetSurface(env, surface);
  observer->Start();
}

/// @brief JNI function to release decoder resources.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_releaseDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv*, jobject) {
  observer->Stop();
}
