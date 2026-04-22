#include "eillance/view_model/settings_view_model.hpp"
#include "test-view-models-common.hpp"

using namespace eillance::view_model;

class SettingsViewModelTest : public ViewModelTest {
 protected:
  void SetUp() override {
    ViewModelTest::SetUp();
    vm_ = std::make_shared<SettingsViewModel>(client_);
  }

  void TearDown() override {
    vm_.reset();
    ViewModelTest::TearDown();
  }

  std::shared_ptr<SettingsViewModel> vm_;
};

struct MockSettingsObserver {
  std::optional<std::string> bootstrap_node_url;
  int timeout = 0;
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  bool updated = false;
  std::mutex mutex;
  uint64_t sub1, sub2;

  MockSettingsObserver() {
    sub1 = EventQueue::Instance().Subscribe<EventSettingsStateUpdated>([this](auto const& e) {
      std::lock_guard<std::mutex> const lock(mutex);
      bootstrap_node_url = e.state.bootstrap_node_url;
      timeout = e.state.peer_inactivity_timeout_seconds;
      updated = true;
    });
    sub2 = EventQueue::Instance().Subscribe<EventConnectionStateChanged>([this](auto const& e) {
      std::lock_guard<std::mutex> const lock(mutex);
      connection_state = e.state;
      updated = true;
    });
  }
  ~MockSettingsObserver() {
    EventQueue::Instance().Unsubscribe(sub1);
    EventQueue::Instance().Unsubscribe(sub2);
  }
};

TEST_F(SettingsViewModelTest, InitialLoad) {
  auto state = vm_->GetState();
  EXPECT_EQ(state.peer_inactivity_timeout_seconds, 120);  // Default
}

TEST_F(SettingsViewModelTest, UpdateUrl) {
  auto observer = std::make_shared<MockSettingsObserver>();

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  std::string new_url = "/ip4/127.0.0.1/tcp/1234";
  vm_->UpdateBootstrapNodeUrl(new_url);

  WaitFor([&] {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    return observer->bootstrap_node_url == new_url;
  });

  std::lock_guard<std::mutex> const lock(observer->mutex);
  EXPECT_EQ(observer->bootstrap_node_url, new_url);
}

TEST_F(SettingsViewModelTest, UpdateTimeout) {
  auto observer = std::make_shared<MockSettingsObserver>();

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  vm_->SetPeerInactivityTimeout(300);

  WaitFor([&] {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    return observer->timeout == 300;
  });

  std::lock_guard<std::mutex> const lock(observer->mutex);
  EXPECT_EQ(observer->timeout, 300);
}

TEST_F(SettingsViewModelTest, ConnectionStateChange) {
  auto observer = std::make_shared<MockSettingsObserver>();

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  // Simulate transport reporting state change
  EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::online});

  WaitFor([&] {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    return observer->connection_state == ServiceConnectionState::online;
  });

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    EXPECT_EQ(observer->connection_state, ServiceConnectionState::online);
  }
}
