/**
 * libdatachannel streamer example
 * Copyright (c) 2020 Filip Klembara (in2core)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef helpers_hpp
#define helpers_hpp

#include "rtc/rtc.hpp"

#include <shared_mutex>

struct SenderTrack {
    std::shared_ptr<rtc::Track> track;
    std::shared_ptr<rtc::RtcpSrReporter> sender;

    SenderTrack(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> sender);
};

struct Connection {
    enum class State {
        Waiting,
        WaitingForVideo,
        WaitingForAudio,
        Ready
    };
    const std::shared_ptr<rtc::PeerConnection> & peerConnection = _peerConnection;
    explicit Connection(std::shared_ptr<rtc::PeerConnection> pc) {
        _peerConnection = pc;
    }
    std::optional<std::shared_ptr<SenderTrack>> video;
    std::optional<std::shared_ptr<SenderTrack>> audio;
    std::optional<std::shared_ptr<rtc::DataChannel>> dataChannel;

    void setState(State state);
    State getState();

    uint32_t rtpStartTimestamp = 0;

private:
    std::shared_mutex _mutex;
    State state = State::Waiting;
    std::string id;
    std::shared_ptr<rtc::PeerConnection> _peerConnection;
};

struct ClientTrack {
    std::string id;
    std::shared_ptr<SenderTrack> trackData;
    ClientTrack(std::string id, std::shared_ptr<SenderTrack> trackData);
};

uint64_t currentTimeInMicroSeconds();

#endif /* helpers_hpp */
