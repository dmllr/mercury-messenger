#pragma once

#include <memory>
#include <shared_mutex>

#include <jni.h>

#include "client_engine.hpp"

// Global shared context for JNI bridges
extern JavaVM* g_javaVm;

auto GetSafeEngine() -> std::shared_ptr<eillance::ClientEngine>;
void SetSafeEngine(std::shared_ptr<eillance::ClientEngine> engine);
