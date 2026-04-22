#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <span>

#include "eillance/transport/data_transport.hpp"

namespace eillance::transport {

struct MockCallLog {
  using Json = nlohmann::json;
  std::vector<std::string> starts;
  std::vector<std::string> terminates;
  std::vector<std::string> bootstrap_urls;
  std::vector<std::pair<ContactID, std::string>> closed_connections;
  std::vector<std::pair<ContactID, std::string>> offers;
  std::vector<std::pair<ContactID, std::string>> answers;

  std::mutex mutex_;

  void LogStart() {
    std::lock_guard<std::mutex> const lock(mutex_);
    starts.emplace_back("Start");
  }
  void LogTerminate() {
    std::lock_guard<std::mutex> const lock(mutex_);
    terminates.emplace_back("Terminate");
  }
  void LogReconnect() {
    std::lock_guard<std::mutex> const lock(mutex_);
    starts.emplace_back("Reconnect");
  }
  void LogBootstrapUrl(std::string const& url) {
    std::lock_guard<std::mutex> const lock(mutex_);
    bootstrap_urls.push_back(url);
  }
  void LogClose(ContactID const& peer_id, std::string const& ctx) {
    std::lock_guard<std::mutex> const lock(mutex_);
    closed_connections.emplace_back(peer_id, ctx);
  }
  void LogOffer(ContactID const& peer_id, std::string const& ctx) {
    std::lock_guard<std::mutex> const lock(mutex_);
    offers.emplace_back(peer_id, ctx);
  }
  void LogAnswer(ContactID const& peer_id, std::string const& ctx) {
    std::lock_guard<std::mutex> const lock(mutex_);
    answers.emplace_back(peer_id, ctx);
  }
};

}  // namespace eillance::transport
