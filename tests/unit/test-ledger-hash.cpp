#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/messaging/types.hpp"

using eillance::messaging::LedgerHash;

TEST(LedgerHashTest, EmptyInitialization) {
  LedgerHash const hash;
  EXPECT_TRUE(hash.IsEmpty());
  EXPECT_EQ(hash.ToString(), std::string(64, '0'));
}

TEST(LedgerHashTest, HexInitialization) {
  std::string const hex(64, 'a');
  LedgerHash const hash(hex);
  EXPECT_FALSE(hash.IsEmpty());
  EXPECT_EQ(hash.ToString(), hex);
}

TEST(LedgerHashTest, InvalidHexLength) {
  EXPECT_THROW(LedgerHash("123"), std::invalid_argument);
}

TEST(LedgerHashTest, InvalidHexCharacter) {
  std::string bad_hex(62, '0');
  bad_hex += "xx";
  EXPECT_THROW(LedgerHash{bad_hex}, std::invalid_argument);
}

TEST(LedgerHashTest, SpanInitialization) {
  std::vector<std::byte> const bytes(32, std::byte{0xbb});
  LedgerHash const hash(bytes);
  EXPECT_FALSE(hash.IsEmpty());
  EXPECT_EQ(hash.ToString(), std::string(64, 'b'));
}

TEST(LedgerHashTest, JsonSerialization) {
  std::string const hex(64, 'c');
  LedgerHash const hash(hex);
  nlohmann::json j = hash;
  EXPECT_EQ(j.get<std::string>(), hex);

  auto hash_out = j.get<LedgerHash>();
  EXPECT_EQ(hash_out, hash);
}
