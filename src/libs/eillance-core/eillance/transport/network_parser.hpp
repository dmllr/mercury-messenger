#pragma once

#include <optional>
#include <string_view>

#include "eillance/transport/network_config.hpp"

namespace eillance::transport {

[[nodiscard]] auto Trim(std::string_view text) noexcept -> std::string_view;
[[nodiscard]] auto IsValidIpv4(std::string_view ip) -> bool;
[[nodiscard]] auto IsValidIpv6(std::string_view ip) -> bool;
[[nodiscard]] auto IsValidHostname(std::string_view host) -> bool;

[[nodiscard]] auto ValidateBootstrapUrl(std::string_view raw) -> bool;
[[nodiscard]] auto ParseTurnServerEntry(std::string_view raw) -> std::optional<ParsedTurnServer>;

}  // namespace eillance::transport
