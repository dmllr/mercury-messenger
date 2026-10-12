#include <algorithm>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
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

    client_ = std::make_shared<Client>(storage_, media_transport);
    std::ignore = client_->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
  }

  storage::MockPersistentStorageImpl* mock_storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

TEST_F(InternalMessagingServiceTest, ProcessSignalingMessage_SyncPropose) {
  nlohmann::json const propose =
      {{"from", peer1.ToString()}, {"type", "sync_propose"}, {"topic", peer1.ToString()}, {"head", std::string(64, '0')}};
  std::string str = propose.dump();
  std::vector<std::byte> bytes(str.size());
  std::memcpy(bytes.data(), str.data(), str.size());

  // Ensure a sync request exists to be resolved
  storage_->AddSyncRequest({TopicID(peer1), peer1, Timestamp::Now(), Timestamp{0}});
  client_->Connect(peer1);

  // Make session cipher ready
  eillance::EventQueue::Instance().Push(EventDataChannelOpen{peer1});
  eillance::crypto::SessionCipher dummy_cipher;
  nlohmann::json const key_msg = {
      {"type", "key_exchange"},
      {"key", dummy_cipher.GetPublicKey()},
      {"is_reply", true},
      {"sync_version", 2}
  };
  std::string key_str = key_msg.dump();
  std::vector<std::byte> key_bytes(key_str.size());
  std::transform(key_str.begin(), key_str.end(), key_bytes.begin(), [](char c){ return static_cast<std::byte>(c); });
  eillance::EventQueue::Instance().Push(EventDataChannelMessage{peer1, key_bytes});
  eillance::EventQueue::Instance().WaitFor([]{ return false; }, std::chrono::milliseconds(50));

  eillance::EventQueue::Instance().Push(EventDataChannelMessage{peer1, bytes});
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        return storage_->GetSyncRequests().empty();
      },
      std::chrono::milliseconds(2000)
  );
  // Assert the sync request was resolved (confirmed)
  EXPECT_TRUE(storage_->GetSyncRequests().empty());
}
