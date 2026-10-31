#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class InternalMessagingServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();

    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    auto data_transport_impl = std::make_unique<transport::MockDataTransport>();
    mock_transport_ = data_transport_impl.get();
    auto data_transport = std::make_shared<transport::DataTransport>(std::move(data_transport_impl));
    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);
    std::ignore = client_->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
  }

  // Simulates a key_exchange from `network_id` and waits for it to be processed.
  void SendKeyExchange(ContactID const& network_id, std::optional<ContactID> const& claimed_master,
                       std::vector<std::byte> const& signature) {
    client_->Connect(network_id);
    eillance::EventQueue::Instance().Push(EventDataChannelOpen{network_id});
    eillance::crypto::SessionCipher const remote_cipher;
    nlohmann::json key_msg = {
        {"type", "key_exchange"},
        {"key", remote_cipher.GetPublicKey()},
        {"is_reply", true},
        {"signature", crypto::ToHex(signature)},
        {"sync_version", 2},
        {"ft_version", 1}
    };
    if (claimed_master) {
      key_msg["m_id"] = claimed_master->ToString();
    }
    std::string const str = key_msg.dump();
    std::vector<std::byte> bytes(str.size());
    std::memcpy(bytes.data(), str.data(), str.size());
    eillance::EventQueue::Instance().Push(EventDataChannelMessage{network_id, bytes});
    eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(100));
  }

  [[nodiscard]] auto IsVerified(ContactID const& network_id) const -> bool {
    auto const peer = mock_transport_->GetPeer(network_id);
    return peer && peer->crcp_verified;
  }

  transport::MockDataTransport* mock_transport_;
  storage::MockPersistentStorageImpl* mock_storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  std::unique_ptr<crypto::Identity> peer_ident_;
  ContactID peer1;
};

TEST_F(InternalMessagingServiceTest, ProcessSignalingMessage_SyncPropose) {
  std::string mnemonic;
  peer_ident_ = eillance::crypto::Identity::CreateNew(mnemonic);
  peer1 = ContactID(peer_ident_->GetPublicKey());

  nlohmann::json const propose = {
      {"from", peer1.ToString()},
      {"type", "sync_start"},
      {"topic", peer1.ToString()},
      {"head", messaging::LedgerHash().ToString()},
      {"size", 0}
  };
  std::string str = propose.dump();
  std::vector<std::byte> bytes(str.size());
  std::memcpy(bytes.data(), str.data(), str.size());

  // Ensure a sync request exists to be resolved
  storage_->AddSyncRequest({TopicID(peer1), peer1, Timestamp::Now(), Timestamp{0}});

  auto sig = peer_ident_->Sign(client_->GetId().Span());
  mock_storage_->UpdateContact(
      {.peer_id = peer1, .status = identity::ContactStatus::confirmed, .signature_from_them = sig}
  );
  client_->Contacts()->ReloadCache();

  client_->Connect(peer1);

  // Make session cipher ready
  eillance::EventQueue::Instance().Push(EventDataChannelOpen{peer1});
  eillance::crypto::SessionCipher const dummy_cipher;
  nlohmann::json const key_msg = {
      {"type", "key_exchange"},
      {"key", dummy_cipher.GetPublicKey()},
      {"is_reply", true},
      {"signature", crypto::ToHex(peer_ident_->Sign(peer1.Span()))},
      {"sync_version", 2}
  };
  std::string key_str = key_msg.dump();
  std::vector<std::byte> key_bytes(key_str.size());
  std::transform(key_str.begin(), key_str.end(), key_bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
  eillance::EventQueue::Instance().Push(EventDataChannelMessage{peer1, key_bytes});
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  eillance::EventQueue::Instance().Push(EventDataChannelMessage{peer1, bytes});
  eillance::EventQueue::Instance().WaitFor(
      [&]() { return storage_->GetSyncRequests().empty(); },
      std::chrono::milliseconds(2000)
  );
  // Assert the sync request was resolved (confirmed)
  EXPECT_TRUE(storage_->GetSyncRequests().empty());
}

// --- Security regression tests (audit C1): key exchange must not accept forged master identities ---

class KeyExchangeSecurityTest : public InternalMessagingServiceTest {
 protected:
  void SetUp() override {
    InternalMessagingServiceTest::SetUp();
    std::string mnemonic;
    contact_ident_ = crypto::Identity::CreateNew(mnemonic);
    contact_id_ = ContactID(contact_ident_->GetPublicKey());
    attacker_ident_ = crypto::Identity::CreateNew(mnemonic);
    attacker_device_ = ContactID(attacker_ident_->GetPublicKey());

    mock_storage_->UpdateContact(
        {.peer_id = contact_id_,
         .status = identity::ContactStatus::confirmed,
         .signature_from_them = contact_ident_->Sign(client_->GetId().Span())}
    );
    client_->Contacts()->ReloadCache();
  }

  std::unique_ptr<crypto::Identity> contact_ident_;
  ContactID contact_id_;
  std::unique_ptr<crypto::Identity> attacker_ident_;
  ContactID attacker_device_;
};

TEST_F(KeyExchangeSecurityTest, RejectsDeviceSelfSignatureClaimingContactMaster) {
  // Former "fallback 3": attacker device signs our device ID with its own key and claims a contact's master ID.
  auto const sig = attacker_ident_->Sign(mock_transport_->device_id_.Span());
  SendKeyExchange(attacker_device_, contact_id_, sig);

  EXPECT_FALSE(IsVerified(attacker_device_));
  EXPECT_EQ(mock_transport_->GetMasterId(attacker_device_), attacker_device_);
}

TEST_F(KeyExchangeSecurityTest, RejectsDeviceSelfSignatureClaimingOurOwnMaster) {
  bool self_device_connected = false;
  auto sub = EventQueue::Instance().Subscribe<EventSelfDeviceConnected>([&](auto const&) {
    self_device_connected = true;
  });

  auto const sig = attacker_ident_->Sign(mock_transport_->device_id_.Span());
  SendKeyExchange(attacker_device_, client_->GetId(), sig);

  EXPECT_FALSE(IsVerified(attacker_device_));
  EXPECT_EQ(mock_transport_->GetMasterId(attacker_device_), attacker_device_);
  EXPECT_FALSE(self_device_connected);
  EventQueue::Instance().Unsubscribe(sub);
}

TEST_F(KeyExchangeSecurityTest, RejectsReplayedContactSignatureFromForeignDevice) {
  // Former "fallback 2": the contact's CRCP signature over our master ID is static and must not authenticate devices.
  auto const replayed = contact_ident_->Sign(client_->GetId().Span());
  SendKeyExchange(attacker_device_, contact_id_, replayed);

  EXPECT_FALSE(IsVerified(attacker_device_));
  EXPECT_EQ(mock_transport_->GetMasterId(attacker_device_), attacker_device_);
}

TEST_F(KeyExchangeSecurityTest, RejectsContactSignatureOverOurDeviceFromForeignDevice) {
  // Former "fallback 1".
  auto const sig = contact_ident_->Sign(mock_transport_->device_id_.Span());
  SendKeyExchange(attacker_device_, contact_id_, sig);

  EXPECT_FALSE(IsVerified(attacker_device_));
  EXPECT_EQ(mock_transport_->GetMasterId(attacker_device_), attacker_device_);
}

TEST_F(KeyExchangeSecurityTest, AcceptsDeviceSignedByClaimedMaster) {
  std::string mnemonic;
  auto const device_ident = crypto::Identity::CreateNew(mnemonic);
  ContactID const device_id(device_ident->GetPublicKey());

  auto const sig = contact_ident_->Sign(device_id.Span());
  SendKeyExchange(device_id, contact_id_, sig);

  EXPECT_TRUE(IsVerified(device_id));
  EXPECT_EQ(mock_transport_->GetMasterId(device_id), contact_id_);
}
