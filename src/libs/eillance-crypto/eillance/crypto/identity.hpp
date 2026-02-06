#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <span>

#include "eillance/crypto/bip39_wordlist.hpp"
#include "eillance/crypto/utils.hpp"
#include "eillance/logging/logging.hpp"

namespace eillance::crypto {

class Identity {
  EvpPkeyPtr pkey_;

  explicit Identity(EvpPkeyPtr pkey)
      : pkey_(std::move(pkey)) {
  }

  // Generate a 64-byte seed from a mnemonic phrase using PBKDF2, then take the first 32 bytes.
  static auto MnemonicToSeed(std::string const& mnemonic) -> std::vector<uint8_t> {
    constexpr auto seed_buffer_size = 64;
    constexpr auto ed25519_seed_size = 32;
    constexpr auto pbkdf2_iterations = 2048;

    std::vector<uint8_t> seed(seed_buffer_size);
    // BIP39 uses "mnemonic" as the salt prefix. Here, just "mnemonic" for simplicity.
    std::string const salt_str = "mnemonic";
    // BIP39 specifies 2048 iterations and SHA512.
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (PKCS5_PBKDF2_HMAC(
            mnemonic.c_str(),
            static_cast<int>(mnemonic.length()),
            reinterpret_cast<unsigned char const*>(salt_str.c_str()),
            static_cast<int>(salt_str.length()),
            pbkdf2_iterations,
            EVP_sha512(),
            seed_buffer_size,
            seed.data()
        ) != 1) {
      return {};
    }
    seed.resize(ed25519_seed_size);
    return seed;
  }

 public:
  Identity(Identity const&) = delete;
  auto operator=(Identity const&) -> Identity& = delete;
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

    std::vector<std::byte> entropy(entropy_bytes);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (RAND_bytes(reinterpret_cast<unsigned char*>(entropy.data()), entropy.size()) != 1) {
      return nullptr;
    }

    auto const hash = Sha256(entropy);
    if (hash.empty()) {
      return nullptr;
    }
    auto const checksum_byte = static_cast<uint8_t>(hash[0]);

    std::vector<bool> bits(total_bits);
    // Populate bits from entropy
    for (int i = 0; i < entropy_bytes; ++i) {
      for (int j = 0; j < 8; ++j) {
        bits[i * 8 + j] = (((static_cast<uint8_t>(entropy[i]) >> (7 - j)) & 1) != 0);
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

  static auto GenerateFromMnemonic(std::string const& mnemonic) -> std::unique_ptr<Identity> {
    auto const seed = MnemonicToSeed(mnemonic);
    if (seed.empty()) {
      return nullptr;
    }

    EVP_PKEY* pkey_ptr = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
    if (pkey_ptr == nullptr) {
      return nullptr;
    }
    return std::unique_ptr<Identity>(new Identity(EvpPkeyPtr(pkey_ptr, EVP_PKEY_free)));
  }

  static auto LoadFromMemory(std::string const& pem_data) -> std::unique_ptr<Identity> {
    BioPtr const bio(BIO_new_mem_buf(pem_data.c_str(), -1), BIO_free);
    if (!bio) {
      return nullptr;
    }
    EVP_PKEY* pkey_ptr = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
    if (pkey_ptr == nullptr) {
      return nullptr;
    }
    return std::unique_ptr<Identity>(new Identity(EvpPkeyPtr(pkey_ptr, EVP_PKEY_free)));
  }

  [[maybe_unused]] static auto LoadFromFile(std::filesystem::path const& path) -> std::unique_ptr<Identity> {
    if (!std::filesystem::exists(path)) {
      return nullptr;
    }
    std::ifstream const file(path, std::ios::binary);
    if (!file) {
      return nullptr;
    }
    std::string const pem_data((std::istreambuf_iterator<char>(file.rdbuf())), std::istreambuf_iterator<char>());
    return LoadFromMemory(pem_data);
  }

  [[nodiscard]] auto SaveToMemory() const -> std::string {
    BioPtr const bio(BIO_new(BIO_s_mem()), BIO_free);
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

  [[nodiscard]] auto Save(std::filesystem::path const& path) const -> bool {
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

  [[nodiscard]] auto Sign(std::span<std::byte const> message) -> std::vector<std::byte> {
    EvpMdCtxPtr const ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) {
      return {};
    }
    if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, pkey_.get()) <= 0) {
      return {};
    }
    size_t sig_len = 0;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_DigestSign(
            ctx.get(),
            nullptr,
            &sig_len,
            reinterpret_cast<unsigned char const*>(message.data()),
            message.size()
        ) <= 0 ||
        sig_len == 0) {
      return {};
    }
    std::vector<std::byte> signature(sig_len);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_DigestSign(
            ctx.get(),
            reinterpret_cast<unsigned char*>(signature.data()),
            &sig_len,
            reinterpret_cast<unsigned char const*>(message.data()),
            message.size()
        ) <= 0) {
      return {};
    }
    signature.resize(sig_len);
    return signature;
  }

  static auto Verify(
      std::span<std::byte const> const pub_key,
      std::span<std::byte const> const message,
      std::span<std::byte const> const signature
  ) -> bool {
    if (pub_key.empty() || signature.empty()) {
      return false;
    }
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EvpPkeyPtr const pkey(
        EVP_PKEY_new_raw_public_key(
            EVP_PKEY_ED25519,
            nullptr,
            reinterpret_cast<unsigned char const*>(pub_key.data()),
            pub_key.size()
        ),
        EVP_PKEY_free
    );
    if (!pkey) {
      return false;
    }
    EvpMdCtxPtr const ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) {
      return false;
    }
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) <= 0) {
      return false;
    }
    // NOLINTNEXTLINE(*-reinterpret-cast)
    return EVP_DigestVerify(
               ctx.get(),
               reinterpret_cast<unsigned char const*>(signature.data()),
               signature.size(),
               reinterpret_cast<unsigned char const*>(message.data()),
               message.size()
           ) == 1;
  }
};

}  // namespace eillance::crypto
