#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/transport/peer.hpp"

using namespace eillance::transport;
using namespace eillance;

TEST(PeerTest, Initialization) {
  ContactID const id("0000000000000000000000000000000000000000000000000000000000000001");
  Peer const peer(id);

  EXPECT_EQ(peer.id, id);
  // Default state
  // Peer is constructed with State::waiting in member initializer, but the header default is waiting.
  // We can't access private members directly but GetState is public.
  // Note: Peer is a struct but GetState uses a mutex.
  // We need a non-const peer to call GetState if it's not const-correct, but looking at header:
  // auto GetState() -> State { ... shared_lock ... }
  // It is not marked const in the provided header [100].
  // So we use a mutable peer for checking.
  Peer mutable_peer(id);
  EXPECT_EQ(mutable_peer.GetState(), Peer::State::waiting);

  EXPECT_FALSE(mutable_peer.data_channel_is_open);
  EXPECT_FALSE(mutable_peer.has_media_peer_connection);
  EXPECT_NE(mutable_peer.session_cipher, nullptr);
  EXPECT_GT(mutable_peer.last_activity_time, 0);
}

TEST(PeerTest, StateTransitions) {
  ContactID const id;
  Peer peer(id);

  peer.SetState(Peer::State::ready);
  EXPECT_EQ(peer.GetState(), Peer::State::ready);

  peer.SetState(Peer::State::waitingForVideo);
  EXPECT_EQ(peer.GetState(), Peer::State::waitingForVideo);
}

TEST(PeerTest, ThreadSafety) {
  ContactID const id;
  Peer peer(id);
  constexpr int kIterations = 100;
  constexpr int kThreadCount = 10;

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  for (int i = 0; i < kThreadCount; ++i) {
    threads.emplace_back([&peer, i]() {
      for (int j = 0; j < kIterations; ++j) {
        // Writer
        if (j % 2 == 0) {
          peer.SetState((i % 2 == 0) ? Peer::State::waiting : Peer::State::ready);
        } else {
          // Reader
          [[maybe_unused]] auto s = peer.GetState();
        }
      }
    });
  }

  for (auto& t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  // Final state check to ensure no deadlock occurred and state is valid
  auto const state = peer.GetState();
  EXPECT_TRUE(state == Peer::State::waiting || state == Peer::State::ready);
}
