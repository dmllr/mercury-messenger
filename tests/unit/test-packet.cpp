#include "eillance/utils/packet.hpp"

#include <gtest/gtest.h>

TEST(PacketTest, DefaultConstructor) {
  eillance::Packet p;
  EXPECT_TRUE(p.empty());
  EXPECT_EQ(p.size(), 0);
}

TEST(PacketTest, ConstructFromBytePointer) {
  std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
  eillance::Packet p(data.data(), data.size());
  EXPECT_FALSE(p.empty());
  EXPECT_EQ(p.size(), 3);
  EXPECT_EQ(p.buffer(), data);
}

TEST(PacketTest, ConstructFromUCharPointer) {
  std::vector<unsigned char> data = {0x04, 0x05, 0x06};
  eillance::Packet p(data.data(), data.size());
  EXPECT_FALSE(p.empty());
  EXPECT_EQ(p.size(), 3);
  EXPECT_EQ(p.data()[0], std::byte{0x04});
  EXPECT_EQ(p.data()[1], std::byte{0x05});
  EXPECT_EQ(p.data()[2], std::byte{0x06});
}

TEST(PacketTest, ConstructFromVector) {
  std::vector<std::byte> data = {std::byte{0x07}, std::byte{0x08}};
  eillance::Packet p(data);
  EXPECT_EQ(p.size(), 2);
  EXPECT_EQ(p.buffer(), data);
}

TEST(PacketTest, Append) {
  eillance::Packet p1({std::byte{0x01}, std::byte{0x02}});
  eillance::Packet p2({std::byte{0x03}, std::byte{0x04}});

  p1.append(p2);

  std::vector<std::byte> expected = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
  EXPECT_EQ(p1.size(), 4);
  EXPECT_EQ(p1.buffer(), expected);
  EXPECT_EQ(p1.timestamp(), p2.timestamp());
}

TEST(PacketTest, Clear) {
  eillance::Packet p({std::byte{0x01}});
  ASSERT_FALSE(p.empty());
  p.clear();
  EXPECT_TRUE(p.empty());
  EXPECT_EQ(p.size(), 0);
}

TEST(PacketTest, DataAccessors) {
  std::vector<unsigned char> data_uchar = {1, 2, 3};
  eillance::Packet p(data_uchar.data(), data_uchar.size());

  const std::byte* data_byte_ptr = p.data();
  const char* data_char_ptr = p.dataAsChars();
  const unsigned char* data_uchar_ptr = p.dataAsUChars();

  EXPECT_EQ(data_byte_ptr[0], std::byte{1});
  EXPECT_EQ(data_char_ptr[1], 2);
  EXPECT_EQ(data_uchar_ptr[2], 3);
}
