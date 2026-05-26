#include <gtest/gtest.h>

#include "eillance/transport/media_transport.hpp"
#include "mock_media_transport.hpp"

using namespace eillance::transport;

TEST(MediaTransportTest, WrapperDelegation) {
  auto mock_log = std::make_shared<MockCallLog>();
  auto mock = std::make_unique<MockMediaTransport>(mock_log);
  MediaTransport const mt(std::move(mock));
  
  eillance::ContactID const id("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  mt.Connect(id);
  mt.CreateOffer(id);
  mt.CreateAnswer(id);
  mt.Disconnect(id);
  
  EXPECT_EQ(mock_log->closed_connections.size(), 1);
}
