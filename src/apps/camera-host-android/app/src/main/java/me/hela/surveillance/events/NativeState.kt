package me.hela.surveillance.events

enum class NativeState(val value: Int) {
  Ready(0),
  Closed(1),
  Failure(2),
  Unexpected(3),

  SignalingStable(100),
  SignalingHaveLocalOffer(101),
  SignalingHaveRemoteOffer(102),
  SignalingHaveLocalPranswer(103),
  SignalingHaveRemotePranswer(104),

  GatheringNew(200),
  GatheringInProgress(201),
  GatheringComplete(202),

  IceNew(300),
  IceChecking(301),
  IceConnected(302),
  IceCompleted(303),
  IceFailed(304),
  IceDisconnected(305),
  IceClosed(306),

  PeerNew(400),
  PeerConnecting(401),
  PeerConnected(402),
  PeerDisconnected(403),
  PeerFailed(404),
  PeerClosed(405);

  companion object {
    fun fromInt(value: Int) = NativeState.values().first { it.value == value }
  }
}
