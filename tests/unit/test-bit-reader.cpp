#include <gtest/gtest.h>

#include "eillance/utils/bit_reader.hpp"

TEST(BitReaderTest, ReadBit) {
  std::uint8_t data[] = {0b10101010, 0b11001100};
  eillance::BitReader reader(data, sizeof(data));

  std::uint32_t bit;
  EXPECT_TRUE(reader.ReadBit(bit));
  EXPECT_EQ(bit, 1);
  EXPECT_TRUE(reader.ReadBit(bit));
  EXPECT_EQ(bit, 0);
  EXPECT_TRUE(reader.ReadBit(bit));
  EXPECT_EQ(bit, 1);
  EXPECT_TRUE(reader.ReadBit(bit));
  EXPECT_EQ(bit, 0);
  EXPECT_TRUE(reader.ReadBit(bit));
  EXPECT_EQ(bit, 1);
  EXPECT_TRUE(reader.ReadBit(bit));
  EXPECT_EQ(bit, 0);
  EXPECT_TRUE(reader.ReadBit(bit));
  EXPECT_EQ(bit, 1);
  EXPECT_TRUE(reader.ReadBit(bit));
  EXPECT_EQ(bit, 0);
}

TEST(BitReaderTest, ReadBits) {
  std::uint8_t data[] = {0b10101010, 0b11001100};
  eillance::BitReader reader(data, sizeof(data));

  EXPECT_EQ(reader.ReadBits(4), 0b1010);
  EXPECT_EQ(reader.ReadBits(4), 0b1010);
  EXPECT_EQ(reader.ReadBits(4), 0b1100);
  EXPECT_EQ(reader.ReadBits(4), 0b1100);
}

TEST(BitReaderTest, ReadUE) {
  std::uint8_t data[] = {0b00010000, 0b00100000, 0b01000000};
  eillance::BitReader reader(data, sizeof(data));

  EXPECT_EQ(reader.ReadUE(), 7);
  EXPECT_EQ(reader.ReadUE(), 7);
  EXPECT_EQ(reader.ReadUE(), 7);
}

TEST(BitReaderTest, ReadSE) {
  std::uint8_t data[] = {0b01100101, 0b00010000};
  eillance::BitReader reader(data, sizeof(data));

  EXPECT_EQ(reader.ReadSE(), -1);
  EXPECT_EQ(reader.ReadSE(), -2);
  EXPECT_EQ(reader.ReadSE(), 4);
}

TEST(BitReaderTest, ReadBit_OutOfBounds) {
  std::uint8_t data[] = {0b10101010};
  eillance::BitReader reader(data, sizeof(data));

  std::uint32_t bit;
  for (int i = 0; i < 8; ++i) {
    EXPECT_TRUE(reader.ReadBit(bit));
  }
  EXPECT_FALSE(reader.ReadBit(bit));  // Should fail after reading all bits
}

TEST(BitReaderTest, ReadBits_OutOfBounds) {
  std::uint8_t data[] = {0b10101010};
  eillance::BitReader reader(data, sizeof(data));

  EXPECT_EQ(reader.ReadBits(10), 0b10101010);  // Should return only available 8 bits
}
