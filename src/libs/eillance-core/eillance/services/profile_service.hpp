#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/client/device.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

class ProfileService {
 public:
  using ConnectionRequestHandler = std::function<void(ContactID const&)>;

  explicit ProfileService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport,
      std::shared_ptr<identity::ContactManager> contact_manager,
      AccountService* account_service,
      MessagingService* messaging_service,
      ConnectionRequestHandler connection_handler
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)),
        manager_(std::move(contact_manager)),
        account_service_(account_service),
        messaging_service_(messaging_service),
        connection_handler_(std::move(connection_handler)) {
  }

  // --- Actions ---

  // Sends my profile to the peer. Queues the action.
  void SendMyProfile(ContactID const& peer_id) {
    nlohmann::json msg;
    msg["type"] = "profile_info";
    msg["display_name"] = account_service_->GetSettings()->GetMyDisplayName().value_or("");
    msg["bio"] = account_service_->GetSettings()->GetMyBio().value_or("");

    auto photo = account_service_->GetSettings()->GetMyPhotoBase64();
    if (photo && !photo->empty()) {
      msg["photo"] = *photo;
    } else {
      msg["photo"] = nullptr;
    }

    storage_->AddPendingAction(peer_id, msg.dump());
    messaging_service_->SendQueuedMessages(peer_id);
  }

  // Requests the peer to send their profile.
  void RequestProfile(ContactID const& peer_id) {
    storage_->AddPendingAction(peer_id, nlohmann::json{{"type", "profile_request"}}.dump());
    messaging_service_->SendQueuedMessages(peer_id);
  }

  // Flushes pending profile actions. In unified system, this just calls the central flush.
  void SyncProfile(ContactID const& peer_id) {
    messaging_service_->SendQueuedMessages(peer_id);
  }

  // Marks all confirmed contacts as needing a profile update (e.g. after I change my name).
  void MarkAllContactsProfileSyncNeeded() {
    auto const contacts = manager_->GetContacts();
    for (auto const& contact : contacts) {
      if (contact.status == identity::ContactStatus::confirmed) {
        SendMyProfile(contact.peer_id);
      }
    }
  }

  // --- Incoming Events ---

  void OnProfileUpdate(ContactID const& peer_id, nlohmann::json const& profile_json) {
    auto get_opt = [&](char const* key) -> std::optional<std::string> {
      if (!profile_json.contains(key)) {
        return std::nullopt;
      }
      if (profile_json[key].is_null()) {
        return "";
      }
      return profile_json[key].get<std::string>();
    };

    auto name = get_opt("display_name");
    auto bio = get_opt("bio");
    auto photo = get_opt("photo");

    auto old = manager_->GetContact(peer_id);
    bool changed = false;
    if (name && (!old || old->display_name != name)) {
      changed = true;
    }
    if (bio && (!old || old->bio != bio)) {
      changed = true;
    }
    if (photo && (!old || old->photo_base64 != photo)) {
      changed = true;
    }

    if (changed) {
      INVARIANT(messaging_service_);
      nlohmann::json diff;
      diff["action"] = "profile_update_diff";
      diff["new"] = {{"display_name", name}, {"bio", bio}, {"photo", photo}};
      if (old) {
        diff["old"] = {{"display_name", old->display_name}, {"bio", old->bio}, {"photo", old->photo_base64}};
      }
      messaging_service_->AddSystemEntry(peer_id, diff.dump(), messaging::MessageDirection::received);
    }

    manager_->UpdateContactProfile(peer_id, name, bio, photo);
    INVARIANT(on_contact_profile_updated_);
    on_contact_profile_updated_(peer_id);
  }

  void OnDeviceProfileReceived(ContactID const& peer_id, nlohmann::json const& profile_json) {
    auto profile = ParseDeviceProfile(profile_json);
    {
      std::lock_guard<std::mutex> const lock(online_peers_mutex_);
      online_peers_[peer_id].id = peer_id.ToString();
      online_peers_[peer_id].profile = profile;
    }
    NotifyPeerListUpdate();
    INVARIANT(on_profile_received_);
    on_profile_received_(peer_id, profile);
  }

  // --- Callbacks ---
  using OnContactProfileUpdatedCallback = std::function<void(ContactID const& peer_id)>;
  using OnPeerListUpdateCallback = std::function<void(std::vector<Device> const&)>;
  using OnProfileReceivedCallback = std::function<void(ContactID const&, Device::Profile const&)>;

  void SetContactProfileUpdatedCallback(OnContactProfileUpdatedCallback cb) {
    on_contact_profile_updated_ = std::move(cb);
  }
  void SetPeerListUpdateCallback(OnPeerListUpdateCallback cb) {
    on_peer_list_update_ = std::move(cb);
  }
  void SetProfileReceivedCallback(OnProfileReceivedCallback cb) {
    on_profile_received_ = std::move(cb);
  }

 private:
  void NotifyPeerListUpdate() const {
    INVARIANT(on_peer_list_update_);
    std::vector<Device> list;
    {
      std::lock_guard<std::mutex> const lock(online_peers_mutex_);
      for (auto const& pair : online_peers_) {
        list.push_back(pair.second);
      }
    }
    on_peer_list_update_(list);
  }

  static auto ParseDeviceProfile(nlohmann::json const& jprofile) -> Device::Profile {
    Device::Profile profile;
    for (auto const& cam_item : jprofile) {
      Device::Camera camera;
      camera.id = cam_item.at("id").get<std::string>();
      camera.name = cam_item.at("name").get<std::string>();
      for (auto const& res_item : cam_item.at("resolutions")) {
        camera.resolutions.push_back({res_item.at("width").get<uint16_t>(), res_item.at("height").get<uint16_t>()});
      }
      profile.push_back(camera);
    }
    return profile;
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> manager_;

  AccountService* account_service_;
  MessagingService* messaging_service_;
  ConnectionRequestHandler connection_handler_;

  std::unordered_map<ContactID, Device> online_peers_;
  mutable std::mutex online_peers_mutex_;

  OnContactProfileUpdatedCallback on_contact_profile_updated_;
  OnPeerListUpdateCallback on_peer_list_update_;
  OnProfileReceivedCallback on_profile_received_;
};

}  // namespace eillance::services
