#pragma once

#include <chrono>
#include <memory>
#include <thread>
#include <tuple>

#include <gtest/gtest.h>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/utils/event_queue.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

// Mock capture implementation for duck-typing to satisfy MediaCallService invariants
class MockCapture {
 public:
  void Configure(Config) {
  }
  void Engage() {
  }
  void Revoke() {
  }
  auto FetchBuffers() -> Packet {
    return Packet();
  }
  auto GetSampleDuration() -> Time::Unit {
    return 10000;
  }
};

class ViewModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Storage
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    // Shared Transport Log
    mock_log_ = std::make_shared<transport::MockCallLog>();

    // Transports
    auto data_transport_impl = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_data_transport_ = data_transport_impl.get();
    auto data_transport = std::make_shared<transport::DataTransport>(std::move(data_transport_impl));

    auto media_transport_impl = std::make_unique<transport::MockMediaTransport>(mock_log_);
    mock_media_transport_ = media_transport_impl.get();
    auto media_transport = std::make_shared<transport::MediaTransport>(std::move(media_transport_impl));

    // Client
    client_ = std::make_shared<Client>(storage_, data_transport, media_transport);

    // Inject Mock Capture
    client_->SetVideoCapture(std::make_unique<MockCapture>());
    client_->SetAudioCapture(std::make_unique<MockCapture>());

    // Identity
    (void)client_->GetAccountService()->GenerateNewIdentity();
    client_->GetAccountService()->UpdateMyProfile("Test User", "Bio", "");

    // Start Client
    client_->Start();
  }

  void TearDown() override {
    if (client_) {
      client_->Terminate();
      client_.reset();
    }
  }

  template <typename Func>
  static void WaitFor(Func condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    EventQueue::Instance().WaitFor(condition, timeout);
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  transport::MockDataTransport* mock_data_transport_{};
  transport::MockMediaTransport* mock_media_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
  ContactID const peer2 = ContactID("0000000000000000000000000000000000000000000000000000000000000002");
};
