#include "eillance/view_model/profile_view_model.hpp"
#include "test-view-models-common.hpp"

using namespace eillance::view_model;

class ProfileViewModelTest : public ViewModelTest {
 protected:
  void SetUp() override {
    ViewModelTest::SetUp();
    vm_ = std::make_shared<ProfileViewModel>(client_);
  }

  std::shared_ptr<ProfileViewModel> vm_;
};

struct MockProfileObserver {
  MyProfile profile;
  bool updated = false;
  std::mutex mutex;
  uint64_t sub_id;

  MockProfileObserver() {
    sub_id = EventQueue::Instance().Subscribe<EventProfileStateUpdated>([this](auto const& e) {
      std::lock_guard<std::mutex> const lock(mutex);
      profile = e.profile;
      updated = true;
    });
  }
  ~MockProfileObserver() {
    EventQueue::Instance().Unsubscribe(sub_id);
  }

  auto GetProfile() -> MyProfile {
    std::lock_guard<std::mutex> const lock(mutex);
    return profile;
  }
};

TEST_F(ProfileViewModelTest, GetInitialProfile) {
  auto p = vm_->GetProfile();
  EXPECT_TRUE(p.identity_exists);
  EXPECT_EQ(p.my_display_name, "Test User");
}

TEST_F(ProfileViewModelTest, UpdateProfile) {
  auto observer = std::make_shared<MockProfileObserver>();

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  vm_->UpdateProfile({.display_name = "New Name", .bio = "New Bio", .photo_base64 = "NewPhoto"});

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
  auto old_id = vm_->GetProfile().my_id;
  auto observer = std::make_shared<MockProfileObserver>();

  (void)vm_->GenerateNewIdentity();

  WaitFor([&] {
    auto p = observer->GetProfile();
    return !p.my_id.IsEmpty() && p.my_id != old_id;
  });

  auto p = observer->GetProfile();
  EXPECT_NE(p.my_id, old_id);
}
