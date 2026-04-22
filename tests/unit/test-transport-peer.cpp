#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/transport/peer.hpp"

using namespace eillance::transport;
using namespace eillance;

TEST(PeerTest, Initialization) {
  ContactID const id("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  Peer const peer(id);

  EXPECT_EQ(peer.id, id);
  EXPECT_FALSE(peer.data_channel_is_open);
  EXPECT_FALSE(peer.has_media_peer_connection);
  EXPECT_FALSE(peer.legacy_prompt_sent);
  EXPECT_NE(peer.session_cipher, nullptr);
  EXPECT_GT(peer.last_activity_time.load(), 0);
  EXPECT_FALSE(peer.pending_offer_sdp.has_value());
}
