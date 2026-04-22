#pragma once

#include <chrono>
#include <memory>
#include <thread>
#include <tuple>

#include <gtest/gtest.h>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/utils/event_queue.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

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
    auto media_transport_impl = std::make_unique<transport::MockMediaTransport>(mock_log_);
    mock_media_transport_ = media_transport_impl.get();
    auto media_transport = std::make_shared<transport::MediaTransport>(std::move(media_transport_impl));

    // Client
    client_ = std::make_shared<Client>(storage_, media_transport);

    // Identity
    (void)client_->GenerateNewIdentity();
    client_->UpdateMyProfile({.display_name = "Test User", .bio = "Bio", .photo_base64 = ""});

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
  transport::MockMediaTransport* mock_media_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
  ContactID const peer2 = ContactID("0000000000000000000000000000000000000000000000000000000000000002");
};
