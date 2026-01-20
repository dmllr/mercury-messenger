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
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "eillance/crypto/bip39_wordlist.hpp"
#include "eillance/logging/logging.hpp"

namespace eillance::crypto {

namespace {

constexpr auto HexCharToInt(char c) -> int {
  if (c >= '0' && c <= '9') { return c - '0'; }
  if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
  if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
  return -1;
}
}  // namespace

/// @brief Convert binary data to a hex string
[[nodiscard]] static auto ToHex(std::span<const std::byte> data) -> std::string {
  static constexpr std::array<char, 16> hexmap = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string hex_str(data.size() * 2, ' ');
  for (size_t i = 0; i < data.size(); ++i) {
    hex_str[2 * i] = hexmap[(static_cast<unsigned char>(data[i]) & 0xF0U) >> 4U];
    hex_str[2 * i + 1] = hexmap[static_cast<unsigned char>(data[i]) & 0x0FU];
  }
  return hex_str;
}

[[nodiscard]] static auto ToHex(const std::vector<std::byte>& data) -> std::string {
  return ToHex(std::span{data});
}

/// @brief Convert a hex string to binary data
[[nodiscard]] static auto FromHex(std::string_view hex) -> std::vector<std::byte> {
  if (hex.length() % 2 != 0) { return {}; }
  std::vector<std::byte> bytes(hex.length() / 2);
  for (size_t i = 0; i < hex.length(); i += 2) {
    const int high = HexCharToInt(hex[i]);
    const int low = HexCharToInt(hex[i + 1]);
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
    constexpr auto seed_buffer_size = 64;
    constexpr auto ed25519_seed_size = 32;
    constexpr auto pbkdf2_iterations = 2048;

    std::vector<uint8_t> seed(seed_buffer_size);
    // BIP39 uses "mnemonic" as the salt prefix. Here, just "mnemonic" for simplicity.
    const std::string salt_str = "mnemonic";
    // BIP39 specifies 2048 iterations and SHA512.
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (PKCS5_PBKDF2_HMAC(mnemonic.c_str(), static_cast<int>(mnemonic.length()),
                          reinterpret_cast<const unsigned char*>(salt_str.c_str()), static_cast<int>(salt_str.length()),
                          pbkdf2_iterations, EVP_sha512(), seed_buffer_size, seed.data()) != 1) {
      return {};
    }
    seed.resize(ed25519_seed_size);
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
    constexpr int entropy_bytes = 16;
    constexpr int checksum_bits = 4;
    constexpr int total_bits = 132;
    constexpr int words_count = 12;
    constexpr int bits_per_word = 11;

    std::vector<uint8_t> entropy(entropy_bytes);
    if (RAND_bytes(entropy.data(), entropy.size()) != 1) {
      return nullptr;
    }

    auto hash = std::array<std::uint8_t, SHA256_DIGEST_LENGTH>{};
    SHA256(entropy.data(), entropy.size(), hash.data());
    const uint8_t checksum_byte = hash[0];

    std::vector<bool> bits(total_bits);
    // Populate bits from entropy
    for (int i = 0; i < entropy_bytes; ++i) {
      for (int j = 0; j < 8; ++j) {
        bits[i * 8 + j] = (((entropy[i] >> (7 - j)) & 1) != 0);
      }
    }
    // Populate bits from checksum (first 4 bits of the hash)
    for (int i = 0; i < checksum_bits; ++i) {
      bits[128U + i] = (((checksum_byte >> (7 - i)) & 1) != 0);
    }

    std::stringstream mnemonic_ss;
    for (int i = 0; i < words_count; ++i) {
      uint16_t index = 0;
      for (int j = 0; j < bits_per_word; ++j) {
        index = (index << 1U) | (bits[i * bits_per_word + j] ? 1 : 0);
      }
      mnemonic_ss << bip39English[index] << (i == (words_count - 1) ? "" : " ");
    }
    mnemonic_out = mnemonic_ss.str();
    return GenerateFromMnemonic(mnemonic_out);
  }

  static auto GenerateFromMnemonic(const std::string& mnemonic) -> std::unique_ptr<Identity> {
    auto seed = MnemonicToSeed(mnemonic);
    if (seed.empty()) {
      return nullptr;
    }

    EVP_PKEY* pkey_ptr = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
    if (pkey_ptr == nullptr) { return nullptr; }
    return std::unique_ptr<Identity>(new Identity(EvpPkeyPtr(pkey_ptr, EVP_PKEY_free)));
  }

  static auto LoadFromMemory(const std::string& pem_data) -> std::unique_ptr<Identity> {
    const BioPtr bio(BIO_new_mem_buf(pem_data.c_str(), -1), BIO_free);
    if (!bio) {
      return nullptr;
    }
    EVP_PKEY* pkey_ptr = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
    if (pkey_ptr == nullptr) {
      return nullptr;
    }
    return std::unique_ptr<Identity>(new Identity(EvpPkeyPtr(pkey_ptr, EVP_PKEY_free)));
  }

  [[maybe_unused]] static auto LoadFromFile(const std::filesystem::path& path) -> std::unique_ptr<Identity> {
    if (!std::filesystem::exists(path)) {
      return nullptr;
    }
    std::ifstream const file(path, std::ios::binary);
    if (!file) {
      return nullptr;
    }
    const std::string pem_data((std::istreambuf_iterator<char>(file.rdbuf())), std::istreambuf_iterator<char>());
    return LoadFromMemory(pem_data);
  }

  [[nodiscard]] auto SaveToMemory() const -> std::string {
    const BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio) {
      return "";
    }
    if (PEM_write_bio_PrivateKey(bio.get(), pkey_.get(), nullptr, nullptr, 0, nullptr, nullptr) <= 0) {
      return "";
    }
    BUF_MEM* buffer_ptr = nullptr;  // NOLINT(*-const-correctness)
    BIO_get_mem_ptr(bio.get(), &buffer_ptr);
    if ((buffer_ptr == nullptr) || (buffer_ptr->data == nullptr) || buffer_ptr->length == 0) {
      return "";
    }
    return {buffer_ptr->data, buffer_ptr->length};
  }

  [[nodiscard]] auto Save(const std::filesystem::path& path) const -> bool {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
      return false;
    }
    file << SaveToMemory();
    return file.good();
  }

  [[nodiscard]] auto GetPublicKey() -> std::vector<std::byte> {
    size_t length = 0;  // NOLINT(*-const-correctness)
    if (EVP_PKEY_get_raw_public_key(pkey_.get(), nullptr, &length) <= 0 || length == 0) {
      return {};
    }
    std::vector<std::byte> pub_key(length);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_PKEY_get_raw_public_key(pkey_.get(), reinterpret_cast<unsigned char*>(pub_key.data()), &length) <= 0) {
      return {};
    }
    return pub_key;
  }

  [[nodiscard]] auto Sign(std::span<const std::byte> message) -> std::vector<std::byte> {
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) {
      return {};
    }
    if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, pkey_.get()) <= 0) {
      return {};
    }
    size_t sig_len = 0;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_DigestSign(ctx.get(), nullptr, &sig_len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) <= 0 || sig_len == 0) {
      return {};
    }
    std::vector<std::byte> signature(sig_len);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()), &sig_len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) <= 0) {
      return {};
    }
    signature.resize(sig_len);
    return signature;
  }

  static auto Verify(const std::span<const std::byte> pub_key, const std::span<const std::byte> message, const std::span<const std::byte> signature) -> bool {
    if (pub_key.empty() || signature.empty()) {
      return false;
    }
    // NOLINTNEXTLINE(*-reinterpret-cast)
    const EvpPkeyPtr pkey(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(pub_key.data()), pub_key.size()), EVP_PKEY_free);
    if (!pkey) {
      return false;
    }
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) {
      return false;
    }
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) <= 0) {
      return false;
    }
    // NOLINTNEXTLINE(*-reinterpret-cast)
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(), reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
  }
};

class SessionCipher {
  std::vector<std::byte> sharedSecret_{};
  EvpPkeyPtr ephemeralKey_;

public:
  SessionCipher() : ephemeralKey_(nullptr, EVP_PKEY_free) { GenerateEphemeralKey(); }

  void Reset() { sharedSecret_.clear(); }

  [[nodiscard]] auto GetPublicKey() -> std::vector<std::byte> {
    size_t length = 0;  // NOLINT(*-const-correctness)
    if (EVP_PKEY_get_raw_public_key(ephemeralKey_.get(), nullptr, &length) <= 0 || length == 0) { return {}; }
    std::vector<std::byte> pub_key(length);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_PKEY_get_raw_public_key(ephemeralKey_.get(), reinterpret_cast<unsigned char*>(pub_key.data()), &length) <= 0) { return {}; }
    return pub_key;
  }

  auto ComputeSharedSecret(const std::vector<std::byte>& peer_public_key_bytes) -> bool {
    // NOLINTNEXTLINE(*-reinterpret-cast)
    const EvpPkeyPtr peer_key(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, reinterpret_cast<const unsigned char*>(peer_public_key_bytes.data()), peer_public_key_bytes.size()), EVP_PKEY_free);
    if (!peer_key) {
      LGE << "Failed to create peer public key";
      return false;
    }

    const EvpPkeyCtxPtr ctx(EVP_PKEY_CTX_new(ephemeralKey_.get(), nullptr), EVP_PKEY_CTX_free);
    if (!ctx) {
      LGE << "Failed to create context for key derivation";
      return false;
    }

    if (EVP_PKEY_derive_init(ctx.get()) <= 0) {
      LGE << "Failed to initialize key derivation";
      return false;
    }

    if (EVP_PKEY_derive_set_peer(ctx.get(), peer_key.get()) <= 0) {
      LGE << "Failed to set peer key";
      return false;
    }

    size_t secret_len{};
    if (EVP_PKEY_derive(ctx.get(), nullptr, &secret_len) <= 0) {
      LGE << "Failed to determine shared secret length";
      return false;
    }

    sharedSecret_.resize(secret_len);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_PKEY_derive(ctx.get(), reinterpret_cast<unsigned char*>(sharedSecret_.data()), &secret_len) <= 0) {
      LGE << "Failed to derive shared secret";
      sharedSecret_.clear();
      return false;
    }
    sharedSecret_.resize(secret_len);
    return true;
  }

  [[nodiscard]] auto IsReady() const noexcept -> bool {
    return !sharedSecret_.empty();
  }

  [[nodiscard]] auto Encrypt(const std::string& plaintext) const -> std::vector<std::byte> {
    if (!IsReady()) {
      return {};
    }

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();
    std::vector<std::byte> ciphertext(plaintext.size() + static_cast<size_t>(EVP_CIPHER_block_size(cipher)));
    std::vector<std::byte> init_vector(static_cast<size_t>(EVP_CIPHER_iv_length(cipher)));
    constexpr auto tag_length = 16;
    std::vector<std::byte> tag(tag_length);

    // NOLINTNEXTLINE(*-reinterpret-cast)
    RAND_bytes(reinterpret_cast<unsigned char*>(init_vector.data()), static_cast<int>(init_vector.size()));

    const EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(init_vector.size()), nullptr);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(sharedSecret_.data()), reinterpret_cast<const unsigned char*>(init_vector.data()));

    int len = 0;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()), &len, reinterpret_cast<const unsigned char*>(plaintext.data()), static_cast<int>(plaintext.size()));
    int ciphertext_len = len;

    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()) + len, &len);
    ciphertext_len += len;

    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, tag_length, tag.data());

    std::vector<std::byte> result;
    result.insert(result.end(), init_vector.begin(), init_vector.end());
    result.insert(result.end(), tag.begin(), tag.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.begin() + ciphertext_len);

    return result;
  }

  [[nodiscard]] auto Decrypt(const std::vector<std::byte>& encrypted_data) const -> std::string {
    if (!IsReady()) { return ""; }

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();
    const auto iv_len = EVP_CIPHER_iv_length(cipher);
    constexpr auto tag_length = 16;

    if (encrypted_data.size() < static_cast<size_t>(iv_len + tag_length)) { return ""; }

    const std::byte* init_vector = encrypted_data.data();
    const std::byte* tag = encrypted_data.data() + iv_len;
    const std::byte* ciphertext = encrypted_data.data() + iv_len + tag_length;
    const auto ciphertext_len = static_cast<int>(encrypted_data.size()) - iv_len - tag_length;

    std::vector<unsigned char> plaintext(ciphertext_len);

    const EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv_len, nullptr);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(sharedSecret_.data()), reinterpret_cast<const unsigned char*>(init_vector));

    int len = 0;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, reinterpret_cast<const unsigned char*>(ciphertext), ciphertext_len);
    int plaintext_len = len;

    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag_length, const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(tag)));

    const auto ret = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len);
    if (ret > 0) {
      plaintext_len += len;
      // NOLINTNEXTLINE(*-reinterpret-cast)
      return {reinterpret_cast<char*>(plaintext.data()), static_cast<size_t>(plaintext_len)};
    }

    return "";
  }


private:
  void GenerateEphemeralKey() {
    const EvpPkeyCtxPtr ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), EVP_PKEY_CTX_free);
    if (!ctx) {
      throw std::runtime_error("Failed to create EVP_PKEY_CTX for X25519");
    }
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0) {
      throw std::runtime_error("Failed to initialize keygen for X25519");
    }
    EVP_PKEY* pkey_ptr = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &pkey_ptr) <= 0) {
      throw std::runtime_error("Failed to generate X25519 key");
    }
    ephemeralKey_.reset(pkey_ptr);
  }
};

}  // namespace eillance::crypto
