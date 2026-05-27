#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/services/ledger_projection_service.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class LedgerProjectionServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));
    auto data_transport = std::make_shared<transport::DataTransport>(std::make_unique<transport::MockDataTransport>());

    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);
    std::ignore = client_->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<Client> client_;
};

TEST_F(LedgerProjectionServiceTest, ProjectMessage) {
  ContactID const peer1("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");

  nlohmann::json const payload_json = {
      {"type", "chat"},
      {"message_id", 123},
      {"is_system", false},
      {"payload", {{"c", "Projected Message"}, {"v", 2}}}
  };

  messaging::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  entry.type = messaging::LedgerActionType::Message;
  entry.payload = payload_json.dump();

  EventQueue::Instance().Push(EventLedgerAppended{entry});

  EventQueue::Instance().WaitFor(
      [&]() {
        auto [messages, total] = client_->GetMessageHistoryPage(peer1, 0, 1);
        return !messages.empty();
      },
      std::chrono::seconds(2)
  );

  auto [messages, total] = client_->GetMessageHistoryPage(peer1, 0, 10);
  bool found = false;
  for (auto const& msg : messages) {
    if (std::holds_alternative<messaging::MessageEntry>(msg.data)) {
      if (std::get<messaging::MessageEntry>(msg.data).content == "Projected Message") {
        found = true;
      }
    }
  }
  EXPECT_TRUE(found);
}
