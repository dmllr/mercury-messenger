#pragma once

#include <chrono>
#include <memory>
#include <thread>
#include <tuple>

#include <gtest/gtest.h>

#include "eillance/client/client.hpp"
#include "eillance/view_model/client_event_router.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

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

    // Transport
    auto transport_impl = std::make_unique<transport::MockTransport>();
    mock_transport_ = transport_impl.get();
    transport_ = std::make_shared<transport::Transport>(std::move(transport_impl));

    // Client
    client_ = std::make_shared<Client>(storage_, transport_);

    // Inject Mock Capture
    client_->SetVideoCapture(std::make_unique<MockCapture>());
    client_->SetAudioCapture(std::make_unique<MockCapture>());

    // Router
    router_ = std::make_shared<view_model::ClientEventRouter>(client_);

    // Identity
    (void)client_->GetAccountService()->GenerateNewIdentity();
    client_->GetAccountService()->UpdateMyProfile("Test User", "Bio", "");

    // Start Client
    client_->Start();
  }

  void TearDown() override {
    if (router_) {
      router_->DetachFromClient();
    }
    router_.reset();

    if (client_) {
      client_->Terminate();
      client_.reset();
    }
  }

  // Helper to wait for a condition to be true (polling with timeout)
  // Increased timeout to 3000ms to handle async DB/Network simulation latency in tests
  template <typename Func>
  static void WaitFor(Func condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    auto const start = std::chrono::steady_clock::now();
    while (!condition()) {
      if (std::chrono::steady_clock::now() - start > timeout) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  transport::MockTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<Client> client_;
  std::shared_ptr<view_model::ClientEventRouter> router_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
  ContactID const peer2 = ContactID("0000000000000000000000000000000000000000000000000000000000000002");
};
