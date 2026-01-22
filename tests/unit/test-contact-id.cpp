#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/identity/contact_id.hpp"

using namespace eillance;

TEST(ContactIDTest, DefaultConstructor) {
  ContactID id;
  EXPECT_TRUE(id.IsEmpty());
  // Default ID is empty, ToString returns ""
  EXPECT_EQ(id.ToString(), "");
}

TEST(ContactIDTest, HexConstructorValid) {
  std::string hex(64, 'a');
  ContactID id(hex);
  EXPECT_FALSE(id.IsEmpty());
  EXPECT_EQ(id.ToString(), hex);
}

TEST(ContactIDTest, HexConstructorInvalidLength) { EXPECT_THROW(ContactID("abc"), std::invalid_argument); }

TEST(ContactIDTest, HexConstructorInvalidChar) {
  std::string invalid_hex(62, '0');
  // 'X' is not valid hex
  invalid_hex += "XX";
  EXPECT_THROW(ContactID{invalid_hex}, std::invalid_argument);
}

TEST(ContactIDTest, ByteVectorConstructor) {
  std::vector<std::byte> bytes(32, std::byte{0xFF});
  ContactID id(bytes);
  EXPECT_FALSE(id.IsEmpty());
  // "ff" repeated 32 times
  std::string expected(64, 'f');
  EXPECT_EQ(id.ToString(), expected);
}

TEST(ContactIDTest, SpanConstructor) {
  std::vector<std::byte> bytes(32, std::byte{0x11});
  std::span<std::byte const> s(bytes);
  ContactID id(s);
  EXPECT_EQ(id.Value()[0], std::byte{0x11});
}

TEST(ContactIDTest, Equality) {
  std::string hex(64, '1');
  ContactID id1(hex);
  ContactID id2(hex);
  ContactID id3(std::string(64, '2'));

  EXPECT_EQ(id1, id2);
  EXPECT_NE(id1, id3);
}

TEST(ContactIDTest, JsonSerialization) {
  std::string hex(64, 'b');
  ContactID id(hex);
  nlohmann::json j = id;
  EXPECT_EQ(j.get<std::string>(), hex);

  ContactID id_out = j.get<ContactID>();
  EXPECT_EQ(id_out, id);
}

TEST(ContactIDTest, JsonDeserializationEmpty) {
  nlohmann::json j = "";
  ContactID id = j.get<ContactID>();
  EXPECT_TRUE(id.IsEmpty());
}
