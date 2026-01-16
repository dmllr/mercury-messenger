#include "call_view_model.hpp"

namespace eillance::view_model {

CallViewModel::CallViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier)
    : client_(std::move(client)), notifier_(std::move(notifier)) {
}

auto CallViewModel::GetState() const -> CallState {
  const std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void CallViewModel::StartVoiceCall(const ContactID& peer_id) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_ = {
        .is_connecting = true,
        .is_in_call = false,
        .is_ringing = false,
        .call_direction = messaging::CallDirection::outgoing,
        .peer_id = peer_id,
        .call_type = messaging::HistoryEntryKind::voiceCall};
  }
  client_->StartVoiceCall(peer_id);
  if (notifier_) notifier_(StateChange::CallState);
}

void CallViewModel::StartVideoCall(const ContactID& peer_id) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_ = {
        .is_connecting = true,
        .is_in_call = false,
        .is_ringing = false,
        .call_direction = messaging::CallDirection::outgoing,
        .peer_id = peer_id,
        .call_type = messaging::HistoryEntryKind::videoCall};
  }
  client_->StartVideoCall(peer_id);
  if (notifier_) notifier_(StateChange::CallState);
}

void CallViewModel::Hangup() {
  ContactID peer_id_to_hangup;
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.is_connecting && !state_.is_in_call && !state_.is_ringing) return;
    peer_id_to_hangup = state_.peer_id;
    state_ = {};
    changed = true;
  }
  if (!peer_id_to_hangup.IsEmpty()) {
    client_->Hangup(peer_id_to_hangup);
  }
  if (changed && notifier_) notifier_(StateChange::CallState);
}

void CallViewModel::AcceptCall() {
  ContactID peer_id;
  messaging::HistoryEntryKind call_kind;
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.is_ringing) return;
    peer_id = state_.peer_id;
    call_kind = state_.call_type;
    state_.is_connecting = false;
    state_.is_in_call = true;
    state_.is_ringing = false;
    changed = true;
  }
  if (!peer_id.IsEmpty()) {
    client_->AcceptCall(peer_id, call_kind);
  }
  if (changed && notifier_) notifier_(StateChange::CallState);
}

void CallViewModel::RejectCall() {
  ContactID peer_id;
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.is_ringing) return;
    peer_id = state_.peer_id;
    state_ = {};
    changed = true;
  }
  if (!peer_id.IsEmpty()) {
    client_->RejectCall(peer_id);
  }
  if (changed && notifier_) notifier_(StateChange::CallState);
}

void CallViewModel::OnIncomingCall(const ContactID& peer_id, messaging::HistoryEntryKind call_type) {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.is_connecting && !state_.is_in_call && !state_.is_ringing) {
      state_.is_connecting = false;
      state_.is_ringing = true;
      state_.peer_id = peer_id;
      state_.call_type = call_type;
      state_.call_direction = messaging::CallDirection::incoming;
      changed = true;
    }
  }
  if (changed && notifier_) notifier_(StateChange::CallState);
}

void CallViewModel::OnCallRinging(const ContactID& peer_id) {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.is_connecting && state_.peer_id == peer_id) {
      state_.is_connecting = false;
      state_.is_ringing = true;
      changed = true;
    }
  }
  if (changed && notifier_) notifier_(StateChange::CallState);
}

void CallViewModel::OnCallAccepted(const ContactID& peer_id) {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if ((state_.is_connecting || state_.is_ringing) &&
        state_.peer_id == peer_id &&
        state_.call_direction == messaging::CallDirection::outgoing) {
      state_.is_connecting = false;
      state_.is_ringing = false;
      state_.is_in_call = true;
      changed = true;
    }
  }
  if (changed && notifier_) notifier_(StateChange::CallState);
}

void CallViewModel::OnCallTerminated(const ContactID& peer_id) {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if ((state_.is_connecting || state_.is_in_call || state_.is_ringing) && state_.peer_id == peer_id) {
      state_ = {};
      changed = true;
    }
  }
  if (changed && notifier_) notifier_(StateChange::CallState);
}

}  // namespace eillance::view_model
