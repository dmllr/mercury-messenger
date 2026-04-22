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

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(InternalMessagingServiceTest, ProcessSignalingMessage_SyncPropose) {
  nlohmann::json const propose =
      {{"from", peer1.ToString()}, {"type", "sync_propose"}, {"topic", peer1.ToString()}, {"head", "000"}};
  std::string str = propose.dump();
  std::vector<std::byte> bytes(str.size());
  std::memcpy(bytes.data(), str.data(), str.size());

  // Ensure a sync request exists to be resolved
  storage_->AddSyncRequest({TopicID(peer1), peer1, Timestamp::Now(), Timestamp{0}});

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
