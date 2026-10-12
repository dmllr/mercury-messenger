#include <gtest/gtest.h>

#include "eillance/transport/data_transport.hpp"
#include "mock_data_transport.hpp"

using namespace eillance::transport;

TEST(DataTransportTest, WrapperDelegation) {
  auto mock = std::make_unique<MockDataTransport>();
  auto* mock_ptr = mock.get();
  DataTransport const dt(std::move(mock));
  
  eillance::ContactID const id("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  dt.SetId(id);
  EXPECT_EQ(mock_ptr->id_, id);
  
  dt.Start();
  dt.Terminate();
}
