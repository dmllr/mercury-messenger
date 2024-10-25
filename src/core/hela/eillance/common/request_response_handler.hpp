#pragma once

#include <iostream>
#include <string>
#include <map>
#include <memory>
#include <future>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <sstream>

class RequestResponseHandler {
  std::shared_ptr<DataChannel> dc_;
  std::map<int, std::shared_ptr<std::promise<std::string>>> pendingRequests_;
  std::thread listenerThread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  int requestId_ = 0;
  bool stop_;

public:
  RequestResponseHandler(std::shared_ptr<DataChannel> dc)
      : dc_(dc), stop_(false) {
    // Start the response listener thread
    listenerThread_ = std::thread(&RequestResponseHandler::responseListener, this);
  }

  ~RequestResponseHandler() {
    stop_ = true;
    cv_.notify_all();
    if (listenerThread_.joinable()) {
      listenerThread_.join();
    }
  }

  std::future<std::string> sendRequest(const std::string& request, int timeoutMs = 200) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto id = requestId_++;
    auto promise = std::make_shared<std::promise<std::string>>();
    pendingRequests_[id] = promise;

    dc_->send(request);

    // Set a timeout for the request
    std::thread([this, id, timeoutMs]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
      std::unique_lock<std::mutex> lock(mutex_);
      auto it = pendingRequests_.find(id);
      if (it != pendingRequests_.end()) {
        it->second->set_value("timeout");
        pendingRequests_.erase(it);
      }
    }).detach();

    return promise->get_future();
  }

private:
  void responseListener() {
    dc_->setMessageCallback([this](const std::string& msg) {
      std::unique_lock<std::mutex> lock(mutex_);
      // Parse the message to extract request ID and response
      std::istringstream iss(msg);
      int id;
      std::string response;
      iss >> id >> response;

      auto it = pendingRequests_.find(id);
      if (it != pendingRequests_.end()) {
        it->second->set_value(response);
        pendingRequests_.erase(it);
      }
    });

    while (!stop_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};
