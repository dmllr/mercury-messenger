#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "eillance/crypto/bip39_wordlist.hpp"
#include "eillance/logging/logging.h"

namespace eillance::crypto {

namespace {
constexpr auto hex_char_to_int(char c) -> int {
  if (c >= '0' && c <= '9') { return c - '0'; }
  if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
  if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
  return -1;
}
}  // namespace

/// @brief Convert binary data to a hex string
[[nodiscard]] static auto ToHex(const std::vector<std::byte>& data) -> std::string {
  constexpr char hex_map[] = "0123456789abcdef";
  std::string hex_str(data.size() * 2, ' ');
  for (size_t i = 0; i < data.size(); ++i) {
    hex_str[2 * i] = hex_map[(static_cast<unsigned char>(data[i]) & 0xF0) >> 4];
    hex_str[2 * i + 1] = hex_map[static_cast<unsigned char>(data[i]) & 0x0F];
  }
  return hex_str;
}

/// @brief Convert a hex string to binary data
[[nodiscard]] static auto FromHex(std::string_view hex) -> std::vector<std::byte> {
  if (hex.length() % 2 != 0) { return {}; }
  std::vector<std::byte> bytes(hex.length() / 2);
  for (size_t i = 0; i < hex.length(); i += 2) {
    const int high = hex_char_to_int(hex[i]);
    const int low = hex_char_to_int(hex[i + 1]);
    if (high == -1 || low == -1) { return {}; }
    bytes[i / 2] = static_cast<std::byte>((static_cast<unsigned char>(high) << 4) | static_cast<unsigned char>(low));
  }
  return bytes;
}

// RAII wrappers for OpenSSL objects
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;


class Identity {
  EvpPkeyPtr pkey_;

  explicit Identity(EvpPkeyPtr pkey) : pkey_(std::move(pkey)) {}

  // Generate a 64-byte seed from a mnemonic phrase using PBKDF2, then take the first 32 bytes.
  static auto MnemonicToSeed(const std::string& mnemonic) -> std::vector<uint8_t> {
    std::vector<uint8_t> seed(64);
    // BIP39 uses "mnemonic" as the salt prefix. Here, just "mnemonic" for simplicity.
    const std::string salt_str = "mnemonic";
    // BIP39 specifies 2048 iterations and SHA512.
    if (PKCS5_PBKDF2_HMAC(mnemonic.c_str(), static_cast<int>(mnemonic.length()),
                          reinterpret_cast<const unsigned char*>(salt_str.c_str()), static_cast<int>(salt_str.length()),
                          2048, EVP_sha512(), 64, seed.data()) != 1) {
      return {};
    }
    // Ed25519 seed is 32 bytes.
    seed.resize(32);
    return seed;
  }

public:
  Identity(const Identity&) = delete;
  auto operator=(const Identity&) -> Identity& = delete;
  Identity(Identity&&) noexcept = default;
  auto operator=(Identity&&) noexcept -> Identity& = default;
  ~Identity() = default;

  static auto CreateNew(std::string& mnemonic_out) -> std::unique_ptr<Identity> {
    // 12 words = 128 bits of entropy + 4 bits of checksum = 132 bits.
    std::vector<uint8_t> entropy(16);  // 128 bits
    if (RAND_bytes(entropy.data(), entropy.size()) != 1) {
      return nullptr;
    }

    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(entropy.data(), entropy.size(), hash);
    uint8_t checksum_byte = hash[0];

    std::vector<bool> bits(132);
    // Populate bits from entropy
    for (int i = 0; i < 16; ++i) {
      for (int j = 0; j < 8; ++j) {
        bits[i * 8 + j] = (entropy[i] >> (7 - j)) & 1;
      }
    }
    // Populate bits from checksum (first 4 bits of the hash)
    for (int i = 0; i < 4; ++i) {
      bits[128 + i] = (checksum_byte >> (7 - i)) & 1;
    }

    std::stringstream mnemonic_ss;
    for (int i = 0; i < 12; ++i) {
      uint16_t index = 0;
      for (int j = 0; j < 11; ++j) {
        index = (index << 1) | (bits[i * 11 + j] ? 1 : 0);
      }
      mnemonic_ss << bip39English[index] << (i == 11 ? "" : " ");
    }
    mnemonic_out = mnemonic_ss.str();
    return GenerateFromMnemonic(mnemonic_out);
  }

  static auto GenerateFromMnemonic(const std::string& mnemonic) -> std::unique_ptr<Identity> {
    auto seed = MnemonicToSeed(mnemonic);
    if (seed.empty()) { return nullptr; }

    EVP_PKEY* pkey_ptr = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
    if (!pkey_ptr) { return nullptr; }
    return std::unique_ptr<Identity>(new Identity(EvpPkeyPtr(pkey_ptr, EVP_PKEY_free)));
  }

  static auto LoadFromMemory(const std::string& pem_data) -> std::unique_ptr<Identity> {
    const BioPtr bio(BIO_new_mem_buf(pem_data.c_str(), -1), BIO_free);
    if (!bio) { return nullptr; }
    EVP_PKEY* pkey_ptr = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
    if (pkey_ptr == nullptr) { return nullptr; }
    return std::unique_ptr<Identity>(new Identity(EvpPkeyPtr(pkey_ptr, EVP_PKEY_free)));
  }

  static auto LoadFromFile(const std::filesystem::path& path) -> std::unique_ptr<Identity> {
    if (!std::filesystem::exists(path)) { return nullptr; }
    std::ifstream file(path, std::ios::binary);
    if (!file) { return nullptr; }
    const std::string pem_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return LoadFromMemory(pem_data);
  }

  [[nodiscard]] auto SaveToMemory() const -> std::string {
    const BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio) { return ""; }
    if (PEM_write_bio_PrivateKey(bio.get(), pkey_.get(), nullptr, nullptr, 0, nullptr, nullptr) <= 0) { return ""; }
    BUF_MEM* buffer_ptr = nullptr;  // NOLINT(*-const-correctness)
    BIO_get_mem_ptr(bio.get(), &buffer_ptr);
    if ((buffer_ptr == nullptr) || (buffer_ptr->data == nullptr) || buffer_ptr->length == 0) { return ""; }
    return {buffer_ptr->data, buffer_ptr->length};
  }

  [[nodiscard]] auto Save(const std::filesystem::path& path) const -> bool {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) { return false; }
    file << SaveToMemory();
    return file.good();
  }

  [[nodiscard]] auto GetId() const -> std::string {
    size_t len = 0;
    if (EVP_PKEY_get_raw_public_key(pkey_.get(), nullptr, &len) <= 0 || len == 0) { return ""; }
    std::vector<std::byte> pub_key(len);
    if (EVP_PKEY_get_raw_public_key(pkey_.get(), reinterpret_cast<unsigned char*>(pub_key.data()), &len) <= 0) { return ""; }
    return ToHex(pub_key);
  }

  [[nodiscard]] auto Sign(const std::string& message) const -> std::vector<std::byte> {
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) { return {}; }
    if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, pkey_.get()) <= 0) { return {}; }
    size_t sig_len = 0;
    if (EVP_DigestSign(ctx.get(), nullptr, &sig_len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) <= 0 || sig_len == 0) { return {}; }
    std::vector<std::byte> signature(sig_len);
    if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()), &sig_len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) <= 0) { return {}; }
    signature.resize(sig_len);
    return signature;
  }

  static auto Verify(const std::string& id, const std::string& message, const std::vector<std::byte>& signature) -> bool {
    auto pub_key_bytes = FromHex(id);
    const EvpPkeyPtr pkey(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(pub_key_bytes.data()), pub_key_bytes.size()), EVP_PKEY_free);
    if (!pkey) { return false; }
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) { return false; }
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) <= 0) { return false; }
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(), reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
  }
};

class SessionCipher {
  std::vector<std::byte> shared_secret_;
  EvpPkeyPtr ephemeral_key_;

public:
  SessionCipher() : ephemeral_key_(nullptr, EVP_PKEY_free) { GenerateEphemeralKey(); }

  [[nodiscard]] auto GetPublicKey() const -> std::vector<std::byte> {
    size_t len = 0;
    if (EVP_PKEY_get_raw_public_key(ephemeral_key_.get(), nullptr, &len) <= 0 || len == 0) { return {}; }
    std::vector<std::byte> pub_key(len);
    if (EVP_PKEY_get_raw_public_key(ephemeral_key_.get(), reinterpret_cast<unsigned char*>(pub_key.data()), &len) <= 0) { return {}; }
    return pub_key;
  }

  auto ComputeSharedSecret(const std::vector<std::byte>& peer_public_key_bytes) -> bool {
    const EvpPkeyPtr peer_key(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, reinterpret_cast<const unsigned char*>(peer_public_key_bytes.data()), peer_public_key_bytes.size()), EVP_PKEY_free);
    if (!peer_key) {
      LOGE << "Failed to create peer public key";
      return false;
    }

    const EvpPkeyCtxPtr ctx(EVP_PKEY_CTX_new(ephemeral_key_.get(), nullptr), EVP_PKEY_CTX_free);
    if (!ctx) {
      LOGE << "Failed to create context for key derivation";
      return false;
    }

    if (EVP_PKEY_derive_init(ctx.get()) <= 0) {
      LOGE << "Failed to initialize key derivation";
      return false;
    }

    if (EVP_PKEY_derive_set_peer(ctx.get(), peer_key.get()) <= 0) {
      LOGE << "Failed to set peer key";
      return false;
    }

    size_t secret_len{};
    if (EVP_PKEY_derive(ctx.get(), nullptr, &secret_len) <= 0) {
      LOGE << "Failed to determine shared secret length";
      return false;
    }

    shared_secret_.resize(secret_len);
    if (EVP_PKEY_derive(ctx.get(), reinterpret_cast<unsigned char*>(shared_secret_.data()), &secret_len) <= 0) {
      LOGE << "Failed to derive shared secret";
      shared_secret_.clear();
      return false;
    }
    shared_secret_.resize(secret_len);
    return true;
  }

  [[nodiscard]] auto IsReady() const noexcept -> bool { return !shared_secret_.empty(); }

  [[nodiscard]] auto Encrypt(const std::string& plaintext) const -> std::vector<std::byte> {
    if (!IsReady()) { return {}; }

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();
    std::vector<std::byte> ciphertext(plaintext.size() + static_cast<size_t>(EVP_CIPHER_block_size(cipher)));
    std::vector<std::byte> iv(static_cast<size_t>(EVP_CIPHER_iv_length(cipher)));
    constexpr auto tag_length = 16;
    std::vector<std::byte> tag(tag_length);  // GCM tag length

    RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), static_cast<int>(iv.size()));

    const EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr);
    EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(shared_secret_.data()), reinterpret_cast<const unsigned char*>(iv.data()));

    int len = 0;
    EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()), &len, reinterpret_cast<const unsigned char*>(plaintext.data()), static_cast<int>(plaintext.size()));
    int ciphertext_len = len;

    EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()) + len, &len);
    ciphertext_len += len;

    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, tag_length, tag.data());

    std::vector<std::byte> result;
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), tag.begin(), tag.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.begin() + ciphertext_len);

    return result;
  }

  [[nodiscard]] auto Decrypt(const std::vector<std::byte>& encrypted_data) const -> std::string {
    if (!IsReady()) { return ""; }

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();
    const auto iv_len = EVP_CIPHER_iv_length(cipher);
    constexpr auto tag_len = 16;

    if (encrypted_data.size() < static_cast<size_t>(iv_len + tag_len)) { return ""; }

    const std::byte* iv = encrypted_data.data();
    const std::byte* tag = encrypted_data.data() + iv_len;
    const std::byte* ciphertext = encrypted_data.data() + iv_len + tag_len;
    const auto ciphertext_len = static_cast<int>(encrypted_data.size()) - iv_len - tag_len;

    std::vector<unsigned char> plaintext(ciphertext_len);

    const EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv_len, nullptr);
    EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(shared_secret_.data()), reinterpret_cast<const unsigned char*>(iv));

    int len = 0;
    EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, reinterpret_cast<const unsigned char*>(ciphertext), ciphertext_len);
    int plaintext_len = len;

    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag_len, const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(tag)));

    const auto ret = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len);
    if (ret > 0) {
      plaintext_len += len;
      return {reinterpret_cast<char*>(plaintext.data()), static_cast<size_t>(plaintext_len)};
    }

    return "";
  }


private:
  void GenerateEphemeralKey() {
    const EvpPkeyCtxPtr ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), EVP_PKEY_CTX_free);
    if (!ctx) { throw std::runtime_error("Failed to create EVP_PKEY_CTX for X25519"); }
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0) { throw std::runtime_error("Failed to initialize keygen for X25519"); }
    EVP_PKEY* pkey_ptr = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &pkey_ptr) <= 0) { throw std::runtime_error("Failed to generate X25519 key"); }
    ephemeral_key_.reset(pkey_ptr);
  }
};

}  // namespace eillance::crypto
