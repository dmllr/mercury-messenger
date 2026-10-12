#include <mutex>

#include "eillance/view_model/call_view_model.hpp"
#include "test-view-models-common.hpp"

using namespace eillance::view_model;

class CallViewModelTest : public ViewModelTest {
 protected:
  void SetUp() override {
    ViewModelTest::SetUp();
    vm_ = std::make_shared<CallViewModel>(client_);
  }

  std::shared_ptr<CallViewModel> vm_;
};

struct MockCallObserver {
  CallState state;
  bool updated = false;
  std::mutex mutex;
  std::uint64_t sub_id;

  MockCallObserver() {
    sub_id = EventQueue::Instance().Subscribe<EventCallStateChanged>([this](auto const& e) {
      std::lock_guard<std::mutex> const lock(mutex);
      state = e.state;
      updated = true;
    });
  }
  ~MockCallObserver() {
    EventQueue::Instance().Unsubscribe(sub_id);
  }

  auto GetState() -> CallState {
    std::lock_guard<std::mutex> const lock(mutex);
    return state;
  }
};

TEST_F(CallViewModelTest, StartCall) {
  auto const observer = std::make_shared<MockCallObserver>();

  vm_->StartVoiceCall(peer1);

  WaitFor([&] { return observer->updated; });
  auto const state = observer->GetState();
  EXPECT_TRUE(observer->updated);
  EXPECT_TRUE(state.is_connecting);
  EXPECT_EQ(state.peer_id, peer1);
  EXPECT_EQ(state.call_direction, messaging::CallDirection::outgoing);
}

TEST_F(CallViewModelTest, IncomingCall) {
  auto const observer = std::make_shared<MockCallObserver>();

  // Ensure clean state before test
  {
    auto s = observer->GetState();
    EXPECT_FALSE(s.is_ringing);
    EXPECT_FALSE(s.is_in_call);
    EXPECT_FALSE(s.is_connecting);
  }

  // Simulate incoming call event
  EventQueue::Instance().Push(EventIncomingCall{peer1, messaging::HistoryEntryKind::voiceCall});

  // Wait for state change to ringing
  WaitFor([&] {
    auto s = observer->GetState();
    return s.is_ringing;
  });

  auto state = observer->GetState();
  EXPECT_TRUE(state.is_ringing);
  EXPECT_EQ(state.call_direction, messaging::CallDirection::incoming);
  EXPECT_EQ(state.peer_id, peer1);
  EXPECT_EQ(state.call_type, messaging::HistoryEntryKind::voiceCall);
}

TEST_F(CallViewModelTest, AcceptCall) {
  auto const observer = std::make_shared<MockCallObserver>();

  // Setup ringing state
  EventQueue::Instance().Push(EventIncomingCall{peer1, messaging::HistoryEntryKind::voiceCall});
  WaitFor([&] { return observer->GetState().is_ringing; });

  // Now accept
  vm_->AcceptCall();

  WaitFor([&] { return observer->GetState().is_in_call; });

  auto state = observer->GetState();
  EXPECT_TRUE(state.is_in_call);
  EXPECT_FALSE(state.is_ringing);
}

TEST_F(CallViewModelTest, Hangup) {
  auto const observer = std::make_shared<MockCallObserver>();

  // Setup in-call state
  EventQueue::Instance().Push(EventIncomingCall{peer1, messaging::HistoryEntryKind::voiceCall});
  WaitFor([&] { return observer->GetState().is_ringing; });

  vm_->AcceptCall();
  WaitFor([&] { return observer->GetState().is_in_call; });

  vm_->Hangup();

  WaitFor([&] {
    auto s = observer->GetState();
    return !s.is_in_call && !s.is_connecting && !s.is_ringing;
  });

  auto state = observer->GetState();
  EXPECT_FALSE(state.is_in_call);
  EXPECT_FALSE(state.is_connecting);
  EXPECT_FALSE(state.is_ringing);
  EXPECT_TRUE(state.peer_id.IsEmpty());  // Should be reset
}
