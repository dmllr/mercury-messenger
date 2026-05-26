#include <gtest/gtest.h>

#include "eillance/transport/protocol.hpp"

using namespace eillance::transport::protocol;

TEST(ProtocolTest, CreateSdp) {
  eillance::ContactID const to("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  auto const json = CreateSdp(values::offer, "v=0\r\n", values::contextMedia, to);
  
  EXPECT_EQ(json[keys::type], values::offer);
  EXPECT_EQ(json[keys::sdp], "v=0\r\n");
  EXPECT_EQ(json[keys::context], values::contextMedia);
  EXPECT_EQ(json[keys::to], to.ToString());
}
