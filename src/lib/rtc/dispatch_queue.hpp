/**
 * libdatachannel streamer example
 * Copyright (c) 2020 Filip Klembara (in2core)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef dispatchqueue_hpp
#define dispatchqueue_hpp

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

class DispatchQueue {
  using fp_t = std::function<void()>;

public:
  explicit DispatchQueue(std::string name, size_t threadCount = 1);
  ~DispatchQueue();

  // dispatch and copy
  void dispatch(const fp_t& op);
  // dispatch and move
  void dispatch(fp_t&& op);

  void removePending();

  // Deleted operations
  DispatchQueue(const DispatchQueue& rhs) = delete;
  auto operator=(const DispatchQueue& rhs) -> DispatchQueue& = delete;
  DispatchQueue(DispatchQueue&& rhs) = delete;
  auto operator=(DispatchQueue&& rhs) -> DispatchQueue& = delete;

private:
  std::string name;
  std::mutex lockMutex;
  std::vector<std::thread> threads;
  std::queue<fp_t> queue;
  std::condition_variable condition;
  bool quit = false;

  void dispatchThreadHandler(void);
};

#endif /* dispatchqueue_hpp */
