#include "bridge_context.hpp"

JavaVM* g_javaVm = nullptr;
std::unique_ptr<eillance::ClientEngine> g_clientEngine{};
