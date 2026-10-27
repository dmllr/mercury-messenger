#include "eillance/transport/network_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace eillance::transport {

namespace {

constexpr size_t maxIpv4Octets = 4;
constexpr size_t maxIpv4OctetLength = 3;
constexpr unsigned int maxOctetValue = 255;

constexpr size_t maxHexWordLength = 4;
constexpr int maxIpv6WordCount = 8;
constexpr int maxIpv6CompressedWordCount = 7;
constexpr size_t maxHostnameLength = 253;
constexpr size_t maxHostnameLabelLength = 63;
constexpr uint32_t maxPortNumber = 65535U;

auto IsHexWord(std::string_view const s) -> bool {
  if (s.empty() || s.size() > maxHexWordLength) {
    return false;
  }
  for (char const c : s) {
    if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
  }
  return true;
}

auto ParseIpv6Section(std::string_view const s, int& word_count, bool const allow_ipv4) -> bool {
  if (s.empty()) {
    return true;
  }

  size_t start = 0;
  while (start < s.size()) {
    auto const colon_pos = s.find(':', start);
    auto const part = (colon_pos == std::string_view::npos) ? s.substr(start) : s.substr(start, colon_pos - start);

    if (colon_pos == std::string_view::npos && allow_ipv4 && part.find('.') != std::string_view::npos) {
      if (!IsValidIpv4(part)) {
        return false;
      }
      word_count += 2;
    } else {
      if (!IsHexWord(part)) {
        return false;
      }
      word_count += 1;
    }

    if (colon_pos == std::string_view::npos) {
      break;
    }
    start = colon_pos + 1;
  }

  return true;
}

}  // namespace

auto Trim(std::string_view text) noexcept -> std::string_view {
  while (!text.empty() &&
         (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
    text.remove_suffix(1);
  }
  return text;
}

auto IsValidIpv4(std::string_view const ip) -> bool {
  if (ip.empty() || ip.front() == '.' || ip.back() == '.') {
    return false;
  }

  size_t start = 0;
  size_t octet_count = 0;
  while (start < ip.size()) {
    if (octet_count >= maxIpv4Octets) {
      return false;
    }
    auto const dot_pos = ip.find('.', start);
    auto const part = (dot_pos == std::string_view::npos) ? ip.substr(start) : ip.substr(start, dot_pos - start);

    if (part.empty() || part.size() > maxIpv4OctetLength) {
      return false;
    }
    if (part.size() > 1 && part.front() == '0') {
      return false;
    }

    unsigned int val = 0;
    for (char const c : part) {
      if (c < '0' || c > '9') {
        return false;
      }
      val = val * 10 + static_cast<unsigned int>(c - '0');
    }
    if (val > maxOctetValue) {
      return false;
    }

    octet_count++;
    if (dot_pos == std::string_view::npos) {
      break;
    }
    start = dot_pos + 1;
  }

  return octet_count == maxIpv4Octets;
}

auto IsValidIpv6(std::string_view const ip) -> bool {
  if (ip.size() < 2) {
    return false;
  }

  if (ip.front() == ':' && !ip.starts_with("::")) {
    return false;
  }
  if (ip.back() == ':' && !ip.ends_with("::")) {
    return false;
  }

  auto const double_colon = ip.find("::");
  if (double_colon != std::string_view::npos) {
    if (ip.find("::", double_colon + 2) != std::string_view::npos) {
      return false;
    }

    auto const head = ip.substr(0, double_colon);
    auto const tail = ip.substr(double_colon + 2);

    int head_words = 0;
    int tail_words = 0;

    if (!ParseIpv6Section(head, head_words, false)) {
      return false;
    }
    if (!ParseIpv6Section(tail, tail_words, true)) {
      return false;
    }

    return (head_words + tail_words) <= maxIpv6CompressedWordCount;
  }

  int total_words = 0;
  if (!ParseIpv6Section(ip, total_words, true)) {
    return false;
  }

  return total_words == maxIpv6WordCount;
}

auto IsValidHostname(std::string_view const host) -> bool {
  if (host.empty() || host.size() > maxHostnameLength) {
    return false;
  }
  if (IsValidIpv4(host)) {
    return true;
  }

  size_t start = 0;
  size_t label_count = 0;
  while (start < host.size()) {
    auto dot_pos = host.find('.', start);
    if (dot_pos == std::string_view::npos) {
      dot_pos = host.size();
    }
    auto const label = host.substr(start, dot_pos - start);
    if (label.empty() || label.size() > maxHostnameLabelLength) {
      return false;
    }
    if (label.front() == '-' || label.back() == '-') {
      return false;
    }
    for (char const c : label) {
      if ((std::isalnum(static_cast<unsigned char>(c)) == 0) && c != '-') {
        return false;
      }
    }
    label_count++;
    start = dot_pos + 1;
  }
  return label_count > 0;
}

auto ValidateBootstrapUrl(std::string_view const raw) -> bool {
  auto const s = Trim(raw);
  if (s.empty() || !s.starts_with('/')) {
    return false;
  }

  std::vector<std::string_view> segments;
  size_t start = 1;
  while (start < s.size()) {
    auto const next = s.find('/', start);
    if (next == std::string_view::npos) {
      segments.push_back(s.substr(start));
      break;
    }
    if (next == start) {
      return false;
    }
    segments.push_back(s.substr(start, next - start));
    start = next + 1;
  }

  if (segments.size() != 4 && segments.size() != 6) {
    return false;
  }

  auto const proto = segments[0];
  auto const host = segments[1];
  auto const trans = segments[2];
  auto const port_str = segments[3];

  if (proto == "ip4" && !IsValidIpv4(host)) return false;
  if (proto == "ip6" && !IsValidIpv6(host)) return false;
  if ((proto == "dns" || proto == "dns4" || proto == "dns6") && !IsValidHostname(host)) return false;
  if (proto != "ip4" && proto != "ip6" && proto != "dns" && proto != "dns4" && proto != "dns6") return false;
  if (trans != "tcp" && trans != "udp" && trans != "quic" && trans != "quic-v1") return false;

  if (segments.size() == 6) {
    auto const p2p_proto = segments[4];
    auto const peer_id = segments[5];
    if (p2p_proto != "p2p" && p2p_proto != "ipfs") return false;
    if (peer_id.empty()) return false;
  }

  if (port_str.empty() ||
      !std::ranges::all_of(port_str, [](char const c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
    return false;
  }

  try {
    auto const port = std::stoul(std::string(port_str));
    return port >= 1 && port <= maxPortNumber;
  } catch (...) {
    return false;
  }
}

auto ParseTurnServerEntry(std::string_view const raw) -> std::optional<ParsedTurnServer> {
  auto s = Trim(raw);
  if (s.empty()) {
    return std::nullopt;
  }

  std::string scheme = "turn";
  if (s.starts_with("turn://")) {
    scheme = "turn";
    s.remove_prefix(7);
  } else if (s.starts_with("turns://")) {
    scheme = "turns";
    s.remove_prefix(8);
  } else if (s.starts_with("stun://")) {
    scheme = "stun";
    s.remove_prefix(7);
  } else if (s.starts_with("stuns://")) {
    scheme = "stuns";
    s.remove_prefix(8);
  } else if (s.starts_with("turn:")) {
    scheme = "turn";
    s.remove_prefix(5);
  } else if (s.starts_with("turns:")) {
    scheme = "turns";
    s.remove_prefix(6);
  } else if (s.starts_with("stun:")) {
    scheme = "stun";
    s.remove_prefix(5);
  } else if (s.starts_with("stuns:")) {
    scheme = "stuns";
    s.remove_prefix(6);
  } else if (s.find("://") != std::string_view::npos) {
    return std::nullopt;
  }

  std::string username;
  std::string credential;

  auto const at_pos = s.find('@');
  if (at_pos != std::string_view::npos) {
    auto const user_pass = s.substr(0, at_pos);
    s.remove_prefix(at_pos + 1);

    auto const colon_pos = user_pass.find(':');
    if (colon_pos != std::string_view::npos) {
      username = std::string(user_pass.substr(0, colon_pos));
      credential = std::string(user_pass.substr(colon_pos + 1));
    } else {
      username = std::string(user_pass);
    }
    if (username.empty() || username.find(' ') != std::string_view::npos) {
      return std::nullopt;
    }
  }

  if (s.empty()) {
    return std::nullopt;
  }

  std::string_view host_port = s;
  std::string_view query;
  auto const query_pos = s.find('?');
  if (query_pos != std::string_view::npos) {
    host_port = s.substr(0, query_pos);
    query = s.substr(query_pos + 1);
  }

  if (host_port.empty() || host_port.find(' ') != std::string_view::npos ||
      host_port.find('/') != std::string_view::npos) {
    return std::nullopt;
  }

  std::string_view host;
  std::string_view port_str;

  if (host_port.starts_with('[')) {
    auto const close_bracket = host_port.find(']');
    if (close_bracket == std::string_view::npos || close_bracket + 2 >= host_port.size() ||
        host_port[close_bracket + 1] != ':') {
      return std::nullopt;
    }
    host = host_port.substr(1, close_bracket - 1);
    port_str = host_port.substr(close_bracket + 2);
    if (!IsValidIpv6(host)) {
      return std::nullopt;
    }
  } else {
    auto const first_colon = host_port.find(':');
    auto const last_colon = host_port.rfind(':');
    if (first_colon == std::string_view::npos || first_colon != last_colon) {
      return std::nullopt;
    }
    host = host_port.substr(0, first_colon);
    port_str = host_port.substr(first_colon + 1);
    if (!IsValidHostname(host)) {
      return std::nullopt;
    }
  }

  if (port_str.empty() ||
      !std::ranges::all_of(port_str, [](char const c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
    return std::nullopt;
  }

  try {
    auto const port = static_cast<std::uint16_t>(std::stoul(std::string(port_str)));
    if (port == 0) {
      return std::nullopt;
    }
  } catch (...) {
    return std::nullopt;
  }

  if (!query.empty() && query != "transport=udp" && query != "transport=tcp") {
    return std::nullopt;
  }

  std::string full_url = scheme + ":" + std::string(host_port);
  if (!query.empty()) {
    full_url += "?" + std::string(query);
  }

  return ParsedTurnServer{
      .url = std::move(full_url),
      .username = std::move(username),
      .credential = std::move(credential)
  };
}

}  // namespace eillance::transport
