#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "eillance/crypto/utils.hpp"
#include "eillance/logging/logging.hpp"

namespace eillance::crypto {

class SessionCipher {
  std::vector<std::byte> sharedSecret_{};
  EvpPkeyPtr ephemeralKey_;

 public:
  SessionCipher()
      : ephemeralKey_(nullptr, EVP_PKEY_free) {
    GenerateEphemeralKey();
  }

  void Reset() {
    sharedSecret_.clear();
  }

  [[nodiscard]] auto GetPublicKey() const -> std::vector<std::byte> {
    size_t length = 0;  // NOLINT(*-const-correctness)
    if (EVP_PKEY_get_raw_public_key(ephemeralKey_.get(), nullptr, &length) <= 0 || length == 0) {
      return {};
    }
    std::vector<std::byte> pub_key(length);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_PKEY_get_raw_public_key(ephemeralKey_.get(), reinterpret_cast<unsigned char*>(pub_key.data()), &length) <=
        0) {
      return {};
    }
    return pub_key;
  }

  auto ComputeSharedSecret(std::vector<std::byte> const& peer_public_key_bytes) -> bool {
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EvpPkeyPtr const peer_key(
        EVP_PKEY_new_raw_public_key(
            EVP_PKEY_X25519,
            nullptr,
            reinterpret_cast<unsigned char const*>(peer_public_key_bytes.data()),
            peer_public_key_bytes.size()
        ),
        EVP_PKEY_free
    );
    if (!peer_key) {
      LGE << "Failed to create peer public key";
      return false;
    }

    EvpPkeyCtxPtr const ctx(EVP_PKEY_CTX_new(ephemeralKey_.get(), nullptr), EVP_PKEY_CTX_free);
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

  [[nodiscard]] auto Encrypt(std::string const& plaintext) const -> std::vector<std::byte> {
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

    EvpCipherCtxPtr const ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(init_vector.size()), nullptr);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_EncryptInit_ex(
        ctx.get(),
        nullptr,
        nullptr,
        reinterpret_cast<unsigned char const*>(sharedSecret_.data()),
        reinterpret_cast<unsigned char const*>(init_vector.data())
    );

    int len = 0;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_EncryptUpdate(
        ctx.get(),
        reinterpret_cast<unsigned char*>(ciphertext.data()),
        &len,
        reinterpret_cast<unsigned char const*>(plaintext.data()),
        static_cast<int>(plaintext.size())
    );
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

  [[nodiscard]] auto Decrypt(std::vector<std::byte> const& encrypted_data) const -> std::string {
    if (!IsReady()) {
      return "";
    }

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();
    auto const iv_len = EVP_CIPHER_iv_length(cipher);
    constexpr auto tag_length = 16;

    if (encrypted_data.size() < static_cast<size_t>(iv_len + tag_length)) {
      return "";
    }

    std::byte const* init_vector = encrypted_data.data();
    std::byte const* tag = encrypted_data.data() + iv_len;
    std::byte const* ciphertext = encrypted_data.data() + iv_len + tag_length;
    auto const ciphertext_len = static_cast<int>(encrypted_data.size()) - iv_len - tag_length;

    std::vector<unsigned char> plaintext(ciphertext_len);

    EvpCipherCtxPtr const ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv_len, nullptr);
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_DecryptInit_ex(
        ctx.get(),
        nullptr,
        nullptr,
        reinterpret_cast<unsigned char const*>(sharedSecret_.data()),
        reinterpret_cast<unsigned char const*>(init_vector)
    );

    int len = 0;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_DecryptUpdate(
        ctx.get(),
        plaintext.data(),
        &len,
        reinterpret_cast<unsigned char const*>(ciphertext),
        ciphertext_len
    );
    int plaintext_len = len;

    // NOLINTNEXTLINE(*-reinterpret-cast)
    EVP_CIPHER_CTX_ctrl(
        ctx.get(),
        EVP_CTRL_GCM_SET_TAG,
        tag_length,
        const_cast<unsigned char*>(reinterpret_cast<unsigned char const*>(tag))
    );

    auto const ret = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len);
    if (ret > 0) {
      plaintext_len += len;
      // NOLINTNEXTLINE(*-reinterpret-cast)
      return {reinterpret_cast<char*>(plaintext.data()), static_cast<size_t>(plaintext_len)};
    }

    return "";
  }

 private:
  void GenerateEphemeralKey() {
    EvpPkeyCtxPtr const ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), EVP_PKEY_CTX_free);
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
