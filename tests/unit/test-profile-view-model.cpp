#include "eillance/view_model/profile_view_model.hpp"
#include "test-view-models-common.hpp"

using namespace eillance::view_model;

class ProfileViewModelTest : public ViewModelTest {
 protected:
  void SetUp() override {
    ViewModelTest::SetUp();
    vm_ = std::make_shared<ProfileViewModel>(client_, router_);
  }

  std::shared_ptr<ProfileViewModel> vm_;
};

struct MockProfileObserver : public ProfileViewModel::Observer {
  MyProfile profile;
  bool updated = false;
  std::mutex mutex;

  void OnProfileUpdated(MyProfile const& p) override {
    std::lock_guard<std::mutex> const lock(mutex);
    profile = p;
    updated = true;
  }

  auto GetProfile() -> MyProfile {
    std::lock_guard<std::mutex> const lock(mutex);
    return profile;
  }
};

TEST_F(ProfileViewModelTest, GetInitialProfile) {
  auto observer = std::make_shared<MockProfileObserver>();
  vm_->SetObserver(observer);

  auto p = observer->GetProfile();
  EXPECT_TRUE(p.identity_exists);
  EXPECT_EQ(p.my_display_name, "Test User");
}

TEST_F(ProfileViewModelTest, UpdateProfile) {
  auto observer = std::make_shared<MockProfileObserver>();
  vm_->SetObserver(observer);

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  vm_->UpdateProfile("New Name", "New Bio", "NewPhoto");

  WaitFor([&] {
    auto p = observer->GetProfile();
    return p.my_display_name == "New Name";
  });

  auto p = observer->GetProfile();
  EXPECT_EQ(p.my_display_name, "New Name");
  EXPECT_EQ(p.my_bio, "New Bio");
  EXPECT_EQ(p.my_photo, "NewPhoto");
}

TEST_F(ProfileViewModelTest, GenerateIdentity) {
  auto observer = std::make_shared<MockProfileObserver>();
  vm_->SetObserver(observer);

  auto old_id = observer->GetProfile().my_id;

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  (void)vm_->GenerateNewIdentity();

  WaitFor([&] {
    auto p = observer->GetProfile();
    return !p.my_id.IsEmpty() && p.my_id != old_id;
  });

  auto p = observer->GetProfile();
  EXPECT_NE(p.my_id, old_id);
}
