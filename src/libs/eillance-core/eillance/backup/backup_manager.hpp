#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include "eillance/crypto/crypto.hpp"
#include "eillance/logging/logging.h"

namespace eillance::backup {

// Implements AES-256-GCM encryption for database backup.
// File format: [16-byte salt][12-byte IV][16-byte GCM tag][encrypted data]
class BackupManager {
public:
  explicit BackupManager(std::string db_path) : dbPath_(std::move(db_path)) {}

  [[nodiscard]] bool CreateBackup(const std::string& output_path, const std::string& password) const {
    if (password.empty()) {
      LGE << "Backup password cannot be empty.";
      return false;
    }

    std::ifstream db_file(dbPath_, std::ios::binary);
    if (!db_file) {
      LGE << "Failed to open database file for backup: " << dbPath_;
      return false;
    }

    const std::vector<unsigned char> plaintext((std::istreambuf_iterator<char>(db_file)), std::istreambuf_iterator<char>());
    db_file.close();

    std::vector<unsigned char> salt(kSaltSize);
    if (RAND_bytes(salt.data(), kSaltSize) != 1) {
      LGE << "Failed to generate salt for backup.";
      return false;
    }

    std::vector<unsigned char> key(kKeySize);
    if (!DeriveKey(password, salt, key)) {
      return false;
    }

    std::vector<unsigned char> iv(kIvSize);
    if (RAND_bytes(iv.data(), kIvSize) != 1) {
      LGE << "Failed to generate IV for backup.";
      return false;
    }

    std::vector<unsigned char> tag(kTagSize);
    std::vector<unsigned char> ciphertext(plaintext.size());
    int ciphertext_len = 0;

    const crypto::EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) { return false;
}
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) { return false;
}
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, kIvSize, nullptr) != 1) { return false;
}
    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1) { return false;
}

    int len = 0;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
      return false;
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + len, &len) != 1) {
      return false;
    }
    ciphertext_len += len;
    ciphertext.resize(ciphertext_len);

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, kTagSize, tag.data()) != 1) {
      return false;
    }

    std::ofstream backup_file(output_path, std::ios::binary);
    if (!backup_file) {
      LGE << "Failed to open output file for backup: " << output_path;
      return false;
    }
    backup_file.write(reinterpret_cast<const char*>(salt.data()), kSaltSize);
    backup_file.write(reinterpret_cast<const char*>(iv.data()), kIvSize);
    backup_file.write(reinterpret_cast<const char*>(tag.data()), kTagSize);
    backup_file.write(reinterpret_cast<const char*>(ciphertext.data()), static_cast<std::streamsize>(ciphertext.size()));

    return true;
  }

  [[nodiscard]] bool RestoreFromBackup(const std::string& input_path, const std::string& password) const {
    if (password.empty()) {
      LGE << "Restore password cannot be empty.";
      return false;
    }

    std::ifstream backup_file(input_path, std::ios::binary);
    if (!backup_file) {
      LGE << "Failed to open backup file for restore: " << input_path;
      return false;
    }

    std::vector<unsigned char> salt(kSaltSize);
    backup_file.read(reinterpret_cast<char*>(salt.data()), kSaltSize);
    if (backup_file.gcount() != kSaltSize) { return false;
}

    std::vector<unsigned char> iv(kIvSize);
    backup_file.read(reinterpret_cast<char*>(iv.data()), kIvSize);
    if (backup_file.gcount() != kIvSize) { return false;
}

    std::vector<unsigned char> tag(kTagSize);
    backup_file.read(reinterpret_cast<char*>(tag.data()), kTagSize);
    if (backup_file.gcount() != kTagSize) { return false;
}

    const std::vector<unsigned char> ciphertext((std::istreambuf_iterator<char>(backup_file)), std::istreambuf_iterator<char>());
    backup_file.close();

    std::vector<unsigned char> key(kKeySize);
    if (!DeriveKey(password, salt, key)) {
      return false;
    }

    std::vector<unsigned char> plaintext(ciphertext.size());
    int plaintext_len = 0;

    const crypto::EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) { return false;
}
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) { return false;
}
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, kIvSize, nullptr) != 1) { return false;
}
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1) { return false;
}

    int len = 0;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
      return false;
    }
    plaintext_len = len;

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, kTagSize, tag.data()) != 1) {
      return false;
    }

    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len) <= 0) {
      LGE << "Failed to decrypt backup: incorrect password or corrupt file.";
      return false;
    }
    plaintext_len += len;
    plaintext.resize(plaintext_len);

    std::ofstream db_file(dbPath_, std::ios::binary | std::ios::trunc);
    if (!db_file) {
      LGE << "Failed to open database file for writing during restore: " << dbPath_;
      return false;
    }
    db_file.write(reinterpret_cast<const char*>(plaintext.data()), static_cast<std::streamsize>(plaintext.size()));

    return true;
  }

private:
  static bool DeriveKey(const std::string& password, const std::vector<unsigned char>& salt, std::vector<unsigned char>& key) {
    // NOLINTNEXTLINE(*-reinterpret-cast)
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.length()), salt.data(), static_cast<int>(salt.size()), kPbkdf2Iterations, EVP_sha256(), static_cast<int>(key.size()), key.data()) != 1) {
      LGE << "Failed to derive key from password.";
      return false;
    }
    return true;
  }

  std::string dbPath_;
  static constexpr int kSaltSize = 16;
  static constexpr int kKeySize = 32;  // AES-256
  static constexpr int kIvSize = 12;   // GCM recommended IV size
  static constexpr int kTagSize = 16;  // GCM tag size
  static constexpr int kPbkdf2Iterations = 100000;
};

}  // namespace eillance::backup
