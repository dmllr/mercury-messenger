/**
 * libdatachannel streamer example
 * Copyright (c) 2020 Filip Klembara (in2core)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <chrono>
#include <thread>
#include <utility>

#include "peer.hpp"
#include "stream.hpp"

Stream::Stream(std::shared_ptr<StreamSource> video, std::shared_ptr<StreamSource> audio) : video(std::move(video)), audio(std::move(audio)) {}

Stream::~Stream() {
  stop();
}

auto Stream::unsafePrepareForSample() -> std::pair<std::shared_ptr<StreamSource>, Stream::StreamSourceType> {
  std::shared_ptr<StreamSource> ss;
  StreamSourceType sst;
  uint64_t nextTime;
  if (audio->getSampleTime_us() < video->getSampleTime_us()) {
    ss = audio;
    sst = StreamSourceType::Audio;
    nextTime = audio->getSampleTime_us();
  } else {
    ss = video;
    sst = StreamSourceType::Video;
    nextTime = video->getSampleTime_us();
  }

  auto currentTime = currentTimeInMicroSeconds();

  auto elapsed = currentTime - startTime;
  if (nextTime > elapsed) {
    auto waitTime = nextTime - elapsed;
    mutex.unlock();
    std::this_thread::sleep_for(std::chrono::microseconds(waitTime));
    mutex.lock();
  }
  return {ss, sst};
}

void Stream::sendSample() {
  std::lock_guard lock(mutex);
  if (!isRunning) {
    return;
  }
  auto ssSST = unsafePrepareForSample();
  auto ss = ssSST.first;
  auto sst = ssSST.second;
  auto sample = ss->getSample();
  sampleHandler(sst, ss->getSampleTime_us(), sample);
  ss->loadNextSample();
  dispatchQueue.dispatch([this]() {
    this->sendSample();
  });
}

void Stream::onSample(std::function<void(StreamSourceType, uint64_t, rtc::binary)> handler) {
  sampleHandler = std::move(handler);
}

void Stream::start() {
  std::lock_guard lock(mutex);
  if (isRunning) {
    return;
  }
  _isRunning = true;
  startTime = currentTimeInMicroSeconds();
  audio->start();
  video->start();
  dispatchQueue.dispatch([this]() {
    this->sendSample();
  });
}

void Stream::stop() {
  std::lock_guard lock(mutex);
  if (!isRunning) {
    return;
  }
  _isRunning = false;
  dispatchQueue.removePending();
  audio->stop();
  video->stop();
}
