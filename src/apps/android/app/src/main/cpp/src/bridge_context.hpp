#pragma once

#include <memory>

#include <jni.h>

#include "client_engine.hpp"

// Global shared context for JNI bridges
extern JavaVM* g_javaVm;
extern std::unique_ptr<eillance::ClientEngine> g_clientEngine;
