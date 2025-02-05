#include "hela/eillance/utils/bit_reader.hpp"

#include <gtest/gtest.h>

TEST(BitReaderTest, ReadBit) {
  std::uint8_t data[] = {0b10101010, 0b11001100};
  BitReader reader(data, sizeof(data));

  std::uint32_t bit;
  EXPECT_TRUE(reader.ReadBit(bit)); EXPECT_EQ(bit, 1);
  EXPECT_TRUE(reader.ReadBit(bit)); EXPECT_EQ(bit, 0);
  EXPECT_TRUE(reader.ReadBit(bit)); EXPECT_EQ(bit, 1);
  EXPECT_TRUE(reader.ReadBit(bit)); EXPECT_EQ(bit, 0);
  EXPECT_TRUE(reader.ReadBit(bit)); EXPECT_EQ(bit, 1);
  EXPECT_TRUE(reader.ReadBit(bit)); EXPECT_EQ(bit, 0);
  EXPECT_TRUE(reader.ReadBit(bit)); EXPECT_EQ(bit, 1);
  EXPECT_TRUE(reader.ReadBit(bit)); EXPECT_EQ(bit, 0);
}

TEST(BitReaderTest, ReadBits) {
  std::uint8_t data[] = {0b10101010, 0b11001100};
  BitReader reader(data, sizeof(data));

  EXPECT_EQ(reader.ReadBits(4), 0b1010);
  EXPECT_EQ(reader.ReadBits(4), 0b1010);
  EXPECT_EQ(reader.ReadBits(4), 0b1100);
  EXPECT_EQ(reader.ReadBits(4), 0b1100);
}

TEST(BitReaderTest, ReadUE) {
  std::uint8_t data[] = {0b00000010, 0b11000000}; // 000000 1 0 11000000 -> UE: 2
  BitReader reader(data, sizeof(data));

  EXPECT_EQ(reader.ReadUE(), 2);
}

TEST(BitReaderTest, ReadSE) {
  std::uint8_t data[] = {0b00000011, 0b00000010}; // UE: 3 -> SE: -1, UE: 2 -> SE: 1
  BitReader reader(data, sizeof(data));

  EXPECT_EQ(reader.ReadSE(), -1);
  EXPECT_EQ(reader.ReadSE(), 1);
}

TEST(BitReaderTest, ReadBit_OutOfBounds) {
  std::uint8_t data[] = {0b10101010};
  BitReader reader(data, sizeof(data));

  std::uint32_t bit;
  for (int i = 0; i < 8; ++i) {
    EXPECT_TRUE(reader.ReadBit(bit));
  }
  EXPECT_FALSE(reader.ReadBit(bit)); // Should fail after reading all bits
}

TEST(BitReaderTest, ReadBits_OutOfBounds) {
  std::uint8_t data[] = {0b10101010};
  BitReader reader(data, sizeof(data));

  EXPECT_EQ(reader.ReadBits(10), 0b10101010); // Should return only available 8 bits
}
