#include "bridge_context.hpp"

JavaVM* g_javaVm = nullptr;

static std::shared_ptr<eillance::ClientEngine> g_clientEngineInstance{};
static std::shared_mutex g_clientEngineMutex;

auto GetSafeEngine() -> std::shared_ptr<eillance::ClientEngine> {
  std::shared_lock<std::shared_mutex> lock(g_clientEngineMutex);
  return g_clientEngineInstance;
}

void SetSafeEngine(std::shared_ptr<eillance::ClientEngine> engine) {
  std::unique_lock<std::shared_mutex> lock(g_clientEngineMutex);
  g_clientEngineInstance = std::move(engine);
}
