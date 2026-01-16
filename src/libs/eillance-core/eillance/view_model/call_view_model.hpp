#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include "eillance/client/client.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class CallViewModel {
public:
  explicit CallViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier);

  [[nodiscard]] auto GetState() const -> CallState;

  // Actions
  void StartVoiceCall(const ContactID& peer_id);
  void StartVideoCall(const ContactID& peer_id);
  void Hangup();
  void AcceptCall();
  void RejectCall();

  // Updates from Client
  void OnIncomingCall(const ContactID& peer_id, messaging::HistoryEntryKind call_type);
  void OnCallRinging(const ContactID& peer_id);
  void OnCallAccepted(const ContactID& peer_id);
  void OnCallTerminated(const ContactID& peer_id);

private:
  std::shared_ptr<Client> client_;
  std::function<void(StateChange)> notifier_;

  mutable std::mutex mutex_;
  CallState state_;
};

}  // namespace eillance::view_model
