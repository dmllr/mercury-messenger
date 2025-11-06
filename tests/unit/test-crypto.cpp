#include "eillance/crypto/crypto.hpp"

#include <gtest/gtest.h>

using namespace eillance::crypto;

TEST(CryptoTest, HexConversion) {
  std::vector<std::byte> data = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  std::string hex_str = "deadbeef";

  EXPECT_EQ(ToHex(data), hex_str);
  EXPECT_EQ(FromHex(hex_str), data);
}

TEST(CryptoTest, HexConversionCaseInsensitive) {
  std::vector<std::byte> data = {std::byte{0xCA}, std::byte{0xFE}};
  std::string hex_str_upper = "CAFE";
  std::string hex_str_lower = "cafe";

  EXPECT_EQ(FromHex(hex_str_upper), data);
  EXPECT_EQ(FromHex(hex_str_lower), data);
}

TEST(CryptoTest, IdentityLifecycle) {
  std::string mnemonic;
  auto identity1 = Identity::CreateNew(mnemonic);
  ASSERT_NE(identity1, nullptr);
  EXPECT_FALSE(mnemonic.empty());

  std::string id1 = identity1->GetId();
  EXPECT_FALSE(id1.empty());

  std::string pem_data = identity1->SaveToMemory();
  EXPECT_FALSE(pem_data.empty());

  auto identity2 = Identity::LoadFromMemory(pem_data);
  ASSERT_NE(identity2, nullptr);

  std::string id2 = identity2->GetId();
  EXPECT_EQ(id1, id2);
}

TEST(CryptoTest, SignAndVerify) {
  std::string mnemonic;
  auto identity = Identity::CreateNew(mnemonic);
  ASSERT_NE(identity, nullptr);
  std::string id = identity->GetId();

  std::string message = "This is a test message for signing.";
  auto signature = identity->Sign(message);
  EXPECT_FALSE(signature.empty());

  EXPECT_TRUE(Identity::Verify(id, message, signature));
}

TEST(CryptoTest, VerificationFailsWithWrongMessage) {
  std::string mnemonic;
  auto identity = Identity::CreateNew(mnemonic);
  ASSERT_NE(identity, nullptr);
  std::string id = identity->GetId();

  std::string message = "Original message.";
  std::string tampered_message = "Tampered message.";
  auto signature = identity->Sign(message);

  EXPECT_FALSE(Identity::Verify(id, tampered_message, signature));
}

TEST(CryptoTest, VerificationFailsWithWrongId) {
  std::string mnemonic1, mnemonic2;
  auto identity1 = Identity::CreateNew(mnemonic1);
  auto identity2 = Identity::CreateNew(mnemonic2);
  ASSERT_NE(identity1, nullptr);
  ASSERT_NE(identity2, nullptr);

  std::string id2 = identity2->GetId();
  std::string message = "A message to sign.";
  auto signature = identity1->Sign(message);

  EXPECT_FALSE(Identity::Verify(id2, message, signature));
}

TEST(CryptoTest, VerificationFailsWithWrongSignature) {
  std::string mnemonic;
  auto identity = Identity::CreateNew(mnemonic);
  ASSERT_NE(identity, nullptr);
  std::string id = identity->GetId();

  std::string message = "Another test message.";
  auto signature = identity->Sign(message);
  ASSERT_FALSE(signature.empty());

  // Tamper with the signature
  signature[0] = static_cast<std::byte>(static_cast<uint8_t>(signature[0]) + 1);

  EXPECT_FALSE(Identity::Verify(id, message, signature));
}

TEST(CryptoTest, SessionCipherKeyExchange) {
  SessionCipher cipher_alice;
  SessionCipher cipher_bob;

  auto alice_pub_key = cipher_alice.GetPublicKey();
  auto bob_pub_key = cipher_bob.GetPublicKey();

  ASSERT_FALSE(alice_pub_key.empty());
  ASSERT_FALSE(bob_pub_key.empty());

  ASSERT_TRUE(cipher_alice.ComputeSharedSecret(bob_pub_key));
  ASSERT_TRUE(cipher_bob.ComputeSharedSecret(alice_pub_key));

  EXPECT_TRUE(cipher_alice.IsReady());
  EXPECT_TRUE(cipher_bob.IsReady());
}

TEST(CryptoTest, SessionCipherEncryptDecrypt) {
  SessionCipher cipher_alice;
  SessionCipher cipher_bob;

  ASSERT_TRUE(cipher_alice.ComputeSharedSecret(cipher_bob.GetPublicKey()));
  ASSERT_TRUE(cipher_bob.ComputeSharedSecret(cipher_alice.GetPublicKey()));

  std::string plaintext = "This is a secret message between Alice and Bob.";
  auto ciphertext = cipher_alice.Encrypt(plaintext);
  ASSERT_FALSE(ciphertext.empty());

  auto decrypted_text = cipher_bob.Decrypt(ciphertext);
  EXPECT_EQ(plaintext, decrypted_text);
}

TEST(CryptoTest, SessionCipherDecryptFails) {
  SessionCipher cipher_alice;
  SessionCipher cipher_bob;
  SessionCipher cipher_eve;

  ASSERT_TRUE(cipher_alice.ComputeSharedSecret(cipher_bob.GetPublicKey()));
  ASSERT_TRUE(cipher_bob.ComputeSharedSecret(cipher_alice.GetPublicKey()));
  ASSERT_TRUE(cipher_eve.ComputeSharedSecret(cipher_alice.GetPublicKey()));

  std::string plaintext = "Top secret!";
  auto ciphertext = cipher_alice.Encrypt(plaintext);

  // Eve tries to decrypt, should fail
  auto eve_decrypted = cipher_eve.Decrypt(ciphertext);
  EXPECT_TRUE(eve_decrypted.empty());

  // Tampered ciphertext, Bob tries to decrypt, should fail
  auto tampered_ciphertext = ciphertext;
  tampered_ciphertext.back() = static_cast<std::byte>(static_cast<uint8_t>(tampered_ciphertext.back()) + 1);
  auto bob_decrypted_tampered = cipher_bob.Decrypt(tampered_ciphertext);
  EXPECT_TRUE(bob_decrypted_tampered.empty());
}
