#pragma once

#include <algorithm>
#include <string>
#include <utility>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/topic_id.hpp"
#include "eillance/messaging/types.hpp"

namespace eillance::messaging {

// Ledger entries are signed by their author over their *content* (not prev_hash/hash: the chain is re-linked
// when histories are merged or repaired). The topic is encoded canonically so the same signature verifies on
// every participant:
//  - group/channel topics are global IDs:            "t|<topic>"
//  - 1:1 topics are stored as "the other party", so the conversation is the unordered pair
//    {owner of the ledger, topic}:                    "p|<min>|<max>"
// Distinct prefixes prevent a signature for one form from being accepted as the other.

inline constexpr auto ledgerSignatureDomain = "eillance-ledger-v1";

[[nodiscard]] inline auto CanonicalGroupTopic(TopicID const& topic) -> std::string {
  return "t|" + topic.ToString();
}

[[nodiscard]] inline auto CanonicalPairTopic(ContactID const& ledger_owner, TopicID const& topic) -> std::string {
  auto a = ledger_owner.ToString();
  auto b = topic.ToString();
  if (b < a) {
    std::swap(a, b);
  }
  return "p|" + a + "|" + b;
}

/// Bytes covered by a ledger entry signature. Payload goes last; every other field has a fixed alphabet.
[[nodiscard]] inline auto LedgerSigningBytes(std::string const& canonical_topic, LedgerEntry const& entry)
    -> std::string {
  std::string out = ledgerSignatureDomain;
  out += '\n';
  out += canonical_topic;
  out += '\n';
  out += entry.author_id.ToString();
  out += '\n';
  out += std::to_string(entry.created_at.value);
  out += '\n';
  out += std::to_string(static_cast<int>(entry.type));
  out += '\n';
  out += entry.payload;
  return out;
}

}  // namespace eillance::messaging
