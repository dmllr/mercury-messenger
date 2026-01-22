#include "eillance/view_model/settings_view_model.hpp"
#include "test-view-models-common.hpp"

using namespace eillance::view_model;

class SettingsViewModelTest : public ViewModelTest {
 protected:
  void SetUp() override {
    ViewModelTest::SetUp();
    vm_ = std::make_shared<SettingsViewModel>(client_, router_);
  }

  std::shared_ptr<SettingsViewModel> vm_;
};

struct MockSettingsObserver : public SettingsViewModel::Observer {
  std::optional<std::string> signaling_url;
  int timeout = 0;
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  bool updated = false;
  std::mutex mutex;

  void OnSettingsUpdated(std::optional<std::string> const& url, int t) override {
    std::lock_guard<std::mutex> const lock(mutex);
    signaling_url = url;
    timeout = t;
    updated = true;
  }

  void OnConnectionStateChanged(ServiceConnectionState state) override {
    std::lock_guard<std::mutex> const lock(mutex);
    connection_state = state;
    updated = true;
  }
};

TEST_F(SettingsViewModelTest, InitialLoad) {
  auto observer = std::make_shared<MockSettingsObserver>();
  vm_->SetObserver(observer);

  EXPECT_TRUE(observer->updated);
  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    EXPECT_EQ(observer->timeout, 120);  // Default
  }
}

TEST_F(SettingsViewModelTest, UpdateUrl) {
  auto observer = std::make_shared<MockSettingsObserver>();
  vm_->SetObserver(observer);

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  std::string new_url = "wss://test.server";
  vm_->UpdateSignalingUrl(new_url);

  WaitFor([&] {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    return observer->signaling_url == new_url;
  });

  std::lock_guard<std::mutex> const lock(observer->mutex);
  EXPECT_EQ(observer->signaling_url, new_url);
}

TEST_F(SettingsViewModelTest, UpdateTimeout) {
  auto observer = std::make_shared<MockSettingsObserver>();
  vm_->SetObserver(observer);

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
  vm_->SetObserver(observer);

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  // Simulate transport reporting state change
  router_->Queue().Push(
      EventStatusChanged{ContactID(), EventSource::system, static_cast<int>(ServiceConnectionState::online)}
  );

  WaitFor([&] {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    return observer->connection_state == ServiceConnectionState::online;
  });

  std::lock_guard<std::mutex> const lock(observer->mutex);
  EXPECT_EQ(observer->connection_state, ServiceConnectionState::online);
}
