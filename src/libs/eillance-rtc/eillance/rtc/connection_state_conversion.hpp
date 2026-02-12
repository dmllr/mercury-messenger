#pragma once

#include <cstdint>

#include "eillance/transport/peer_connection_state.hpp"
#include "eillance/utils/invariant.hpp"
#include "rtc/peerconnection.hpp"

namespace eillance::transport {

constexpr auto ToEillanceState(::rtc::PeerConnection::State const state) -> PeerConnectionState {
  switch (state) {
    case ::rtc::PeerConnection::State::New:
      return PeerConnectionState::newConnection;
    case ::rtc::PeerConnection::State::Connecting:
      return PeerConnectionState::connecting;
    case ::rtc::PeerConnection::State::Connected:
      return PeerConnectionState::connected;
    case ::rtc::PeerConnection::State::Disconnected:
      return PeerConnectionState::disconnected;
    case ::rtc::PeerConnection::State::Failed:
      return PeerConnectionState::failed;
    case ::rtc::PeerConnection::State::Closed:
      return PeerConnectionState::closed;
  }
  INVARIANT_UNREACHABLE();
}

}  // namespace eillance::transport
