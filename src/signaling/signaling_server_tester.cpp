#include <csignal>
#include <memory>
#include <string>
#include <thread>

#include "plog/Appenders/ColorConsoleAppender.h"
#include "plog/Formatters/TxtFormatter.h"
#include "plog/Init.h"
#include <plog/Log.h>
#include <rtc/websocket.hpp>

struct Log {
  static constexpr auto tag = "helaeillance";
  static constexpr auto logLevel = plog::info;

private:
  // NOLINTNEXTLINE(*-statically-constructed-objects)
  inline static plog::ColorConsoleAppender<plog::TxtFormatter> appender;
  inline const static plog::Logger<0>& logger = plog::init(logLevel, &appender);
};

std::atomic<bool> running(true); // NOLINT(*-avoid-non-const-global-variables)

void SignalHandler(int signum) {
  LOGI << "\nInterrupt signal (" << signum << ") received.\n";
  running = false;
}

int main(int argc, char* argv[]) {
  auto ws_config = rtc::WebSocketConfiguration{
      .disableTlsVerification = true,
      .pingInterval = std::chrono::milliseconds(1000),
  };

  auto ws_ = std::make_unique<rtc::WebSocket>(ws_config);
  ws_->onOpen([]() {
    LOGI << "OnWebSocketOpen();";
  });
  ws_->onClosed([]() {
    LOGI << "OnWebSocketClosed();";
  });
  ws_->onError([](const std::string& error) {
    LOGI << "OnWebSocketError(error); " << error;
  });
  ws_->onMessage(nullptr, [](const std::string& data) {
    LOGI << "OnWebSocketMessage(data);" << data;
  });

  // Register signal handler
  std::signal(SIGINT, SignalHandler);

  ws_->open("wss://127.0.0.1:8888");

  LOGI << "Press Ctrl+C to exit";

  // Keep the program running until SIGINT (Ctrl+C) is received
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  LOGI << "Server stopped";
  return 0;
}
