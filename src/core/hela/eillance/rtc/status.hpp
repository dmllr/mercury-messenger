#pragma once

#include <rtc/peerconnection.hpp>
#include "hela/eillance/events/service_state.h"

namespace hela::eillance {

struct Status final {
  ServiceState service_state{ServiceState::unknown};
  rtc::PeerConnection::State state{rtc::PeerConnection::State::New};
  rtc::PeerConnection::IceState ice_state{rtc::PeerConnection::IceState::New};
  rtc::PeerConnection::GatheringState gathering_state{rtc::PeerConnection::GatheringState::New};
  rtc::PeerConnection::SignalingState signalling_state{rtc::PeerConnection::SignalingState::Stable};
};

}  // namespace hela::eillance
