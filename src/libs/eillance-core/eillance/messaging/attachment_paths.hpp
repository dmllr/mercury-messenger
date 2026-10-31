#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace eillance::messaging {

/// Maximum length (bytes) of a sanitized attachment file name.
inline constexpr std::size_t maxAttachmentFileNameLength = 200;
/// Maximum length of an attachment ID received from a peer.
inline constexpr std::size_t maxAttachmentIdLength = 64;

/// Attachment IDs are used to build local file paths, so peers may only send IDs
/// consisting of [A-Za-z0-9_-] with a bounded length.
[[nodiscard]] inline auto IsValidAttachmentId(std::string_view const id) -> bool {
  if (id.empty() || id.size() > maxAttachmentIdLength) {
    return false;
  }
  return std::ranges::all_of(id, [](char const c) -> bool {
    auto const uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '_' || c == '-';
  });
}

/// Turns an untrusted file name into a single, safe path component.
/// Strips any directory part, control and reserved characters, leading/trailing dots and spaces,
/// and Windows device names. Returns an empty string if nothing usable remains.
[[nodiscard]] inline auto SanitizeAttachmentFileName(std::string_view name) -> std::string {
  // Keep only the last path component (handle both separators regardless of platform).
  if (auto const pos = name.find_last_of("/\\"); pos != std::string_view::npos) {
    name.remove_prefix(pos + 1);
  }

  std::string out;
  out.reserve(name.size());
  for (char const c : name) {
    auto const uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc == 0x7F) {
      continue;
    }
    if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*') {
      out.push_back('_');
      continue;
    }
    out.push_back(c);
  }

  // Trim leading/trailing dots and spaces (rules out "", ".", "..", hidden files and Windows oddities).
  auto const first = out.find_first_not_of(". ");
  if (first == std::string::npos) {
    return {};
  }
  auto const last = out.find_last_not_of(". ");
  out = out.substr(first, last - first + 1);

  // Bound the length, preserving the extension when reasonable, without splitting a UTF-8 sequence.
  if (out.size() > maxAttachmentFileNameLength) {
    std::string ext;
    if (auto const dot = out.rfind('.'); dot != std::string::npos && out.size() - dot <= 16) {
      ext = out.substr(dot);
    }
    std::size_t cut = maxAttachmentFileNameLength - ext.size();
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0U) == 0x80U) {
      --cut;
    }
    out = out.substr(0, cut) + ext;
  }

  // Windows reserved device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9), compared on the stem.
  std::string stem = out.substr(0, out.find('.'));
  std::ranges::transform(stem, stem.begin(), [](unsigned char const c) { return static_cast<char>(std::toupper(c)); });
  static constexpr std::array<std::string_view, 4> reserved = {"CON", "PRN", "AUX", "NUL"};
  bool const is_reserved =
      std::ranges::find(reserved, stem) != reserved.end() ||
      (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9');
  if (is_reserved) {
    out.insert(out.begin(), '_');
  }
  return out;
}

/// True if `path` resolves (lexically, after normalization) to a location strictly inside `dir`.
[[nodiscard]] inline auto IsPathInsideDirectory(std::filesystem::path const& dir, std::filesystem::path const& path)
    -> bool {
  std::error_code ec;
  auto const base = std::filesystem::weakly_canonical(std::filesystem::absolute(dir, ec), ec).lexically_normal();
  if (ec) {
    return false;
  }
  auto const target = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec).lexically_normal();
  if (ec) {
    return false;
  }
  auto const rel = target.lexically_relative(base);
  if (rel.empty() || rel == ".") {
    return false;
  }
  auto const first = *rel.begin();
  return first != ".." && !rel.is_absolute();
}

}  // namespace eillance::messaging
