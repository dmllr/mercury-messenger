#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/rtc/transport_impl_libdatachannel.hpp"

// Access private members for testing
class TestRtcTransport : public eillance::rtc::RtcTransport {
 public:
  // Expose config for verification
  // Since we cannot easily inspect private members without friend access or reflection,
  // we rely on the fact that SetIceServers is now public/overridden.
  // We can't check the internal rtcConfig_ directly unless we modify the class to be test-friendly.
  // However, we can at least ensure it does not throw or crash on valid/invalid inputs.
  // Real verification of the config content would require inspecting the `rtcConfig_` member
  // which is private. For this test, we assume if it parses without error it's a good sign,
  // and checking if logging infrastructure doesn't report failure is secondary.

  // NOTE: In a real codebase we might add a GetConfig() for testing or use friend classes.
  using eillance::rtc::RtcTransport::SetIceServers;
};

TEST(RtcTransportTest, SetIceServers_ValidConfig) {
  TestRtcTransport transport;

  // JSON array of objects as sent by the server
  nlohmann::json const config = nlohmann::json::array(
      {{{"urls", "stun:stun1.example.com"}},
       {{"urls", "turn:turn.example.com"}, {"username", "user"}, {"credential", "pass"}}}
  );

  EXPECT_NO_THROW(transport.SetIceServers(config.dump()));
}

TEST(RtcTransportTest, SetIceServers_HandlesSingleUrlString) {
  TestRtcTransport transport;
  nlohmann::json const config = nlohmann::json::array({{{"urls", "stun:stun1.example.com"}}});
  EXPECT_NO_THROW(transport.SetIceServers(config.dump()));
}

TEST(RtcTransportTest, SetIceServers_HandlesUrlArray) {
  TestRtcTransport transport;
  nlohmann::json const config =
      nlohmann::json::array({{{"urls", nlohmann::json::array({"stun:stun1.example.com", "stun:stun2.example.com"})}}});
  EXPECT_NO_THROW(transport.SetIceServers(config.dump()));
}

TEST(RtcTransportTest, SetIceServers_IgnoresInvalidFormat) {
  TestRtcTransport transport;
  // Passing an object instead of array should log error but not crash
  nlohmann::json const invalid = {{"urls", "stun:invalid"}};
  EXPECT_NO_THROW(transport.SetIceServers(invalid.dump()));
}

TEST(RtcTransportTest, SetIceServers_EmptyArray) {
  TestRtcTransport transport;
  EXPECT_NO_THROW(transport.SetIceServers("[]"));
}

TEST(RtcTransportTest, SetIceServers_MalformedNestedArray) {
  TestRtcTransport transport;
  // Simulate the issue: [ [] ]
  // Should not crash, just ignore the inner array
  EXPECT_NO_THROW(transport.SetIceServers("[[]]"));
}

TEST(RtcTransportTest, SetIceServers_MixedContent) {
  TestRtcTransport transport;
  // Valid entry + invalid entry
  std::string const raw_json = R"([{"urls": "stun:valid.com"}, [], "invalid_string", {"urls": "stun:valid2.com"}])";
  EXPECT_NO_THROW(transport.SetIceServers(raw_json));
}
