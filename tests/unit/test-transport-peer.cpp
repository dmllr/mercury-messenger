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
  EXPECT_FALSE(peer.data_channel_is_open);
  EXPECT_FALSE(peer.has_media_peer_connection);
  EXPECT_FALSE(peer.legacy_prompt_sent);
  EXPECT_NE(peer.session_cipher, nullptr);
  EXPECT_GT(peer.last_activity_time.load(), 0);
  EXPECT_FALSE(peer.pending_offer_sdp.has_value());
}
