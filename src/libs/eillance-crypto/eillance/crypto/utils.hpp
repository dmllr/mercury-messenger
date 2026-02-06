#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <span>

namespace eillance::crypto {

// RAII wrappers for OpenSSL objects
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

namespace {
constexpr auto HexCharToInt(char c) -> int {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}
}  // namespace

/// @brief Convert binary data to a hex string
[[nodiscard]] static auto ToHex(std::span<std::byte const> data) -> std::string {
  static constexpr std::array<char, 16> hexmap =
      {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string hex_str(data.size() * 2, ' ');
  for (size_t i = 0; i < data.size(); ++i) {
    hex_str[2 * i] = hexmap[(static_cast<unsigned char>(data[i]) & 0xF0U) >> 4U];
    hex_str[2 * i + 1] = hexmap[static_cast<unsigned char>(data[i]) & 0x0FU];
  }
  return hex_str;
}

[[nodiscard]] static auto ToHex(std::vector<std::byte> const& data) -> std::string {
  return ToHex(std::span{data});
}

/// @brief Convert a hex string to binary data
[[nodiscard]] static auto FromHex(std::string_view hex) -> std::vector<std::byte> {
  if (hex.length() % 2 != 0) {
    return {};
  }
  std::vector<std::byte> bytes(hex.length() / 2);
  for (size_t i = 0; i < hex.length(); i += 2) {
    int const high = HexCharToInt(hex[i]);
    int const low = HexCharToInt(hex[i + 1]);
    if (high == -1 || low == -1) {
      return {};
    }
    bytes[i / 2] = static_cast<std::byte>((static_cast<unsigned char>(high) << 4) | static_cast<unsigned char>(low));
  }
  return bytes;
}

/// @brief Calculates SHA256 hash of the given data using OpenSSL EVP API.
[[nodiscard]] static auto Sha256(std::span<std::byte const> const data) -> std::vector<std::byte> {
  EvpMdCtxPtr const ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!ctx) {
    return {};
  }

  if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
    return {};
  }

  if (EVP_DigestUpdate(ctx.get(), data.data(), data.size()) != 1) {
    return {};
  }

  std::vector<std::byte> hash(EVP_MD_size(EVP_sha256()));
  unsigned int hash_len = 0;
  // NOLINTNEXTLINE(*-reinterpret-cast)
  if (EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(hash.data()), &hash_len) != 1) {
    return {};
  }
  hash.resize(hash_len);
  return hash;
}

/// @brief Calculates SHA256 hash and returns it as a hex string.
[[nodiscard]] static auto Sha256Hex(std::span<std::byte const> const data) -> std::string {
  return ToHex(Sha256(data));
}

/// @brief Calculates SHA256 hash of a string view and returns it as a hex string.
[[nodiscard]] static auto Sha256Hex(std::string_view const data) -> std::string {
  // NOLINTNEXTLINE(*-reinterpret-cast)
  auto const* ptr = reinterpret_cast<std::byte const*>(data.data());
  return Sha256Hex(std::span<std::byte const>(ptr, data.size()));
}

/// @brief Calculates SHA256 hash of a file.
[[nodiscard]] static auto CalculateFileHash(std::filesystem::path const& path) -> std::string {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return "";
  }

  EvpMdCtxPtr const ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!ctx) {
    return "";
  }

  if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
    return "";
  }

  std::array<char, 4096> buffer{};
  while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
    if (EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<size_t>(file.gcount())) != 1) {
      return "";
    }
    if (!file) {
      break;
    }
  }

  std::vector<std::byte> hash(EVP_MD_size(EVP_sha256()));
  unsigned int hash_len = 0;
  // NOLINTNEXTLINE(*-reinterpret-cast)
  if (EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(hash.data()), &hash_len) != 1) {
    return "";
  }
  hash.resize(hash_len);
  return ToHex(hash);
}

}  // namespace eillance::crypto
