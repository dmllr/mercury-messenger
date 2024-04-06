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

#ifdef _MSC_VER
// taken from https://stackoverflow.com/questions/10905892/equivalent-of-gettimeday-for-windows
#include <winsock2.h> // for struct timeval
#include <windows.h>

namespace {

struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};

int gettimeofday(struct timeval *tv, struct timezone *tz) {
	if (tv) {
		FILETIME filetime; /* 64-bit value representing the number of 100-nanosecond intervals since
		                      January 1, 1601 00:00 UTC */
		ULARGE_INTEGER x;
		ULONGLONG usec;
		static const ULONGLONG epoch_offset_us =
		    11644473600000000ULL; /* microseconds betweeen Jan 1,1601 and Jan 1,1970 */

#if _WIN32_WINNT >= _WIN32_WINNT_WIN8
		GetSystemTimePreciseAsFileTime(&filetime);
#else
		GetSystemTimeAsFileTime(&filetime);
#endif
		x.LowPart = filetime.dwLowDateTime;
		x.HighPart = filetime.dwHighDateTime;
		usec = x.QuadPart / 10 - epoch_offset_us;
		tv->tv_sec = time_t(usec / 1000000ULL);
		tv->tv_usec = long(usec % 1000000ULL);
	}
	if (tz) {
		TIME_ZONE_INFORMATION timezone;
		GetTimeZoneInformation(&timezone);
		tz->tz_minuteswest = timezone.Bias;
		tz->tz_dsttime = 0;
	}
	return 0;
}
#else
#include <sys/time.h>
#endif

}


struct Peer {
    struct Track {
        std::shared_ptr<rtc::Track> track;
        std::shared_ptr<rtc::RtcpSrReporter> sender;
        std::shared_ptr<rtc::RtcpReceivingSession> session;

        Track(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> sender) {
            this->track = track;
            this->sender = sender;
            this->session = nullptr;
        }

        Track(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpReceivingSession> session) {
            this->track = track;
            this->sender = nullptr;
            this->session = session;
        }
    };

    enum class State {
        Waiting,
        WaitingForVideo,
        WaitingForAudio,
        Ready
    };
    const std::shared_ptr<rtc::PeerConnection> & peerConnection = _peerConnection;
    explicit Peer(std::shared_ptr<rtc::PeerConnection> pc) {
        _peerConnection = pc;
    }
    std::optional<std::shared_ptr<Track>> video;
    std::optional<std::shared_ptr<Track>> audio;
    std::optional<std::shared_ptr<rtc::DataChannel>> dataChannel;

    void setState(State state) {
        std::unique_lock lock(_mutex);
        this->state = state;
    }

    State getState() {
        std::shared_lock lock(_mutex);
        return state;
    }

    uint32_t rtpStartTimestamp = 0;

private:
    std::shared_mutex _mutex;
    State state = State::Waiting;
    std::string id;
    std::shared_ptr<rtc::PeerConnection> _peerConnection;
};

inline uint64_t currentTimeInMicroSeconds() {
    struct timeval time{};
    gettimeofday(&time, nullptr);
    return uint64_t(time.tv_sec) * 1000 * 1000 + time.tv_usec;
}

#endif /* helpers_hpp */
