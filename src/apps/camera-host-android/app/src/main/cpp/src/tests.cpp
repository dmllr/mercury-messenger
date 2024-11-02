#include <gtest/gtest.h>
#include <jni.h>

JavaVM* javaVm;

int main(int argc, char *argv[])
{
#if RUN_GTEST
  INIT_GTESTS(&argc,(char**)argv);
  RUN_ALL_GTESTS();
#endif
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  JNIEnv* env;
  if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }
  javaVm = vm;
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnLoad(JavaVM* jvm, void* reserved) {
  JNIEnv* env;
  if (jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return;
  }
}
