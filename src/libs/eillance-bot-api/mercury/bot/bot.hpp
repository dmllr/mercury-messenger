#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "mercury/bot/types.hpp"

namespace eillance {
class Client;
}

namespace mercury::bot {

class Bot {
 public:
  struct Options {
    std::filesystem::path identity_path;
    bool auto_accept_contacts = true;
    bool auto_reject_calls = true;
  };

  explicit Bot(Options options);
  // Constructor for dependency injection (testing)
  Bot(std::shared_ptr<eillance::Client> client, Options options);
  ~Bot();

  Bot(Bot const&) = delete;
  Bot(Bot&&) = delete;
  auto operator=(Bot const&) -> Bot& = delete;
  auto operator=(Bot&&) -> Bot& = delete;

  // --- Actions ---

  // Starts the bot (non-blocking).
  void Start() const;

  // Stops the bot and releases resources.
  void Stop() const;

  // Sends a text message to the specified chat (user ID).
  void SendMessage(std::string const& chat_id, std::string const& text) const;

  // Marks a specific message as read, sending a read receipt to the sender.
  void MarkAsRead(std::string const& chat_id, std::uint64_t message_id) const;

  // --- Events ---

  // Register a callback for incoming text messages.
  void OnMessage(std::function<void(Message const&)> handler) const;

  // --- Information ---

  // Returns the bot's own ID.
  [[nodiscard]] auto GetId() const -> std::string;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mercury::bot
