#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/rtc/data_transport_impl_libdatachannel.hpp"

// Access private members for testing via inheritance
class TestRtcDataTransport : public eillance::rtc::RtcDataTransport {
 public:
  using eillance::rtc::RtcDataTransport::SetIceServers;
};

TEST(RtcTransportTest, SetIceServers_ValidConfig) {
  TestRtcDataTransport transport;

  nlohmann::json const config = nlohmann::json::array(
      {{{"urls", "stun:stun1.example.com"}},
       {{"urls", "turn:turn.example.com"}, {"username", "user"}, {"credential", "pass"}}}
  );

  EXPECT_NO_THROW(transport.SetIceServers(config.dump()));
}

TEST(RtcTransportTest, SetIceServers_HandlesSingleUrlString) {
  TestRtcDataTransport transport;
  nlohmann::json const config = nlohmann::json::array({{{"urls", "stun:stun1.example.com"}}});
  EXPECT_NO_THROW(transport.SetIceServers(config.dump()));
}

TEST(RtcTransportTest, SetIceServers_HandlesUrlArray) {
  TestRtcDataTransport transport;
  nlohmann::json const config =
      nlohmann::json::array({{{"urls", nlohmann::json::array({"stun:stun1.example.com", "stun:stun2.example.com"})}}});
  EXPECT_NO_THROW(transport.SetIceServers(config.dump()));
}

TEST(RtcTransportTest, SetIceServers_IgnoresInvalidFormat) {
  TestRtcDataTransport transport;
  nlohmann::json const invalid = {{"urls", "stun:invalid"}};
  EXPECT_NO_THROW(transport.SetIceServers(invalid.dump()));
}

TEST(RtcTransportTest, SetIceServers_EmptyArray) {
  TestRtcDataTransport transport;
  EXPECT_NO_THROW(transport.SetIceServers("[]"));
}

TEST(RtcTransportTest, SetIceServers_MalformedNestedArray) {
  TestRtcDataTransport transport;
  EXPECT_NO_THROW(transport.SetIceServers("[[]]"));
}

TEST(RtcTransportTest, SetIceServers_MixedContent) {
  TestRtcDataTransport transport;
  std::string const raw_json = R"([{"urls": "stun:valid.com"}, [], "invalid_string", {"urls": "stun:valid2.com"}])";
  EXPECT_NO_THROW(transport.SetIceServers(raw_json));
}
