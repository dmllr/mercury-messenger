#pragma once

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "hela/eillance/logging/logging.h"

namespace hela::eillance::crypto {

/// @brief Convert binary data to a hex string
static std::string ToHex(const std::vector<std::byte>& data) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (std::byte b: data) {
    ss << std::setw(2) << static_cast<int>(b);
  }
  return ss.str();
}

/// @brief Convert a hex string to binary data
static std::vector<std::byte> FromHex(const std::string& hex) {
  std::vector<std::byte> bytes;
  for (unsigned int i = 0; i < hex.length(); i += 2) {
    std::string byteString = hex.substr(i, 2);
    auto byte_val = static_cast<unsigned char>(strtol(byteString.c_str(), nullptr, 16));
    bytes.push_back(std::byte{byte_val});
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

public:
  Identity(const Identity&) = delete;
  Identity& operator=(const Identity&) = delete;
  Identity(Identity&&) = default;
  Identity& operator=(Identity&&) = default;

  static std::unique_ptr<Identity> CreateNew() {
    const EvpPkeyCtxPtr ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free);
    if (!ctx) {
      return nullptr;
    }
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0) {
      return nullptr;
    }
    EVP_PKEY* pkey_ptr = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &pkey_ptr) <= 0) {
      return nullptr;
    }
    return std::unique_ptr<Identity>(new Identity(EvpPkeyPtr(pkey_ptr, EVP_PKEY_free)));
  }

  static std::unique_ptr<Identity> LoadFromMemory(const std::string& pem_data) {
    BioPtr bio(BIO_new_mem_buf(pem_data.c_str(), -1), BIO_free);
    if (!bio) {
      return nullptr;
    }
    EVP_PKEY* pkey_ptr = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
    if (!pkey_ptr) {
      return nullptr;
    }
    return std::unique_ptr<Identity>(new Identity(EvpPkeyPtr(pkey_ptr, EVP_PKEY_free)));
  }

  static std::unique_ptr<Identity> LoadFromFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
      return nullptr;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return nullptr;
    }
    std::string pem_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return LoadFromMemory(pem_data);
  }

  std::string SaveToMemory() const {
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio) {
      return "";
    }
    if (PEM_write_bio_PrivateKey(bio.get(), pkey_.get(), nullptr, nullptr, 0, nullptr, nullptr) <= 0) {
      return "";
    }
    BUF_MEM* buffer_ptr = nullptr;
    BIO_get_mem_ptr(bio.get(), &buffer_ptr);
    if (!buffer_ptr || !buffer_ptr->data || buffer_ptr->length == 0) {
      return "";
    }
    return {buffer_ptr->data, buffer_ptr->length};
  }

  bool Save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
      return false;
    }
    file << SaveToMemory();
    return file.good();
  }

  std::string GetId() const {
    size_t len = 0;
    if (EVP_PKEY_get_raw_public_key(pkey_.get(), nullptr, &len) <= 0 || len == 0) {
      return "";
    }
    std::vector<std::byte> pub_key(len);
    if (EVP_PKEY_get_raw_public_key(pkey_.get(), reinterpret_cast<unsigned char*>(pub_key.data()), &len) <= 0) {
      return "";
    }
    return ToHex(pub_key);
  }

  std::vector<std::byte> Sign(const std::string& message) const {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) {
      return {};
    }
    if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, pkey_.get()) <= 0) {
      return {};
    }
    size_t sig_len = 0;
    if (EVP_DigestSign(ctx.get(), nullptr, &sig_len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) <= 0 || sig_len == 0) {
      return {};
    }
    std::vector<std::byte> signature(sig_len);
    if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()), &sig_len, reinterpret_cast<const unsigned char*>(message.data()), message.size()) <= 0) {
      return {};
    }
    signature.resize(sig_len);
    return signature;
  }

  static bool Verify(const std::string& id, const std::string& message, const std::vector<std::byte>& signature) {
    auto pub_key_bytes = FromHex(id);
    EvpPkeyPtr pkey(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(pub_key_bytes.data()), pub_key_bytes.size()), EVP_PKEY_free);
    if (!pkey) {
      return false;
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) {
      return false;
    }
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) <= 0) {
      return false;
    }
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(), reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
  }
};

class SessionCipher {
  std::vector<std::byte> shared_secret_;
  EvpPkeyPtr ephemeral_key_;

public:
  SessionCipher() : ephemeral_key_(nullptr, EVP_PKEY_free) {
    GenerateEphemeralKey();
  }

  std::vector<std::byte> GetPublicKey() const {
    size_t len = 0;
    if (EVP_PKEY_get_raw_public_key(ephemeral_key_.get(), nullptr, &len) <= 0 || len == 0) {
      return {};
    }
    std::vector<std::byte> pub_key(len);
    if (EVP_PKEY_get_raw_public_key(ephemeral_key_.get(), reinterpret_cast<unsigned char*>(pub_key.data()), &len) <= 0) {
      return {};
    }
    return pub_key;
  }

  bool ComputeSharedSecret(const std::vector<std::byte>& peer_public_key_bytes) {
    EvpPkeyPtr peer_key(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, reinterpret_cast<const unsigned char*>(peer_public_key_bytes.data()), peer_public_key_bytes.size()), EVP_PKEY_free);
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

    size_t secret_len;
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

  auto IsReady() const -> bool {
    return !shared_secret_.empty();
  }

  auto Encrypt(const std::string& plaintext) const -> std::vector<std::byte> {
    if (!IsReady()) return {};

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();
    std::vector<std::byte> ciphertext(plaintext.size() + EVP_CIPHER_block_size(cipher));
    std::vector<std::byte> iv(EVP_CIPHER_iv_length(cipher));
    std::vector<std::byte> tag(16);  // GCM tag length

    RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), iv.size());

    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr);
    EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(shared_secret_.data()), reinterpret_cast<const unsigned char*>(iv.data()));

    int len = 0;
    EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()), &len, reinterpret_cast<const unsigned char*>(plaintext.data()), static_cast<int>(plaintext.size()));
    int ciphertext_len = len;

    EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()) + len, &len);
    ciphertext_len += len;

    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, reinterpret_cast<unsigned char*>(tag.data()));

    std::vector<std::byte> result;
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), tag.begin(), tag.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.begin() + ciphertext_len);

    return result;
  }

  auto Decrypt(const std::vector<std::byte>& encrypted_data) const -> std::string {
    if (!IsReady()) return "";

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();
    int iv_len = EVP_CIPHER_iv_length(cipher);
    int tag_len = 16;

    if (encrypted_data.size() < static_cast<size_t>(iv_len + tag_len)) {
      return "";
    }

    const std::byte* iv = encrypted_data.data();
    const std::byte* tag = encrypted_data.data() + iv_len;
    const std::byte* ciphertext = encrypted_data.data() + iv_len + tag_len;
    int ciphertext_len = static_cast<int>(encrypted_data.size()) - iv_len - tag_len;

    std::vector<unsigned char> plaintext(ciphertext_len);

    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv_len, nullptr);
    EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(shared_secret_.data()), reinterpret_cast<const unsigned char*>(iv));

    int len = 0;
    EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, reinterpret_cast<const unsigned char*>(ciphertext), ciphertext_len);
    int plaintext_len = len;

    EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag_len, const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(tag)));

    int ret = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len);
    if (ret > 0) {
      plaintext_len += len;
      return std::string(reinterpret_cast<char*>(plaintext.data()), plaintext_len);
    } else {
      return "";
    }
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
    ephemeral_key_.reset(pkey_ptr);
  }
};

}  // namespace hela::eillance::crypto
