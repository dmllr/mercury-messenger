#include <thread>

#include <gtest/gtest.h>

#include "eillance/client/client.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/ledger_projection_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/services/sync_engine.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class ContactServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    auto mock_dt = std::make_unique<transport::MockDataTransport>();
    mock_dt_ = mock_dt.get();
    auto data_transport = std::make_shared<transport::DataTransport>(std::move(mock_dt));
    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);
    std::ignore = client_->GenerateNewIdentity();
    client_->UpdateMyProfile({.display_name = "Test User", .bio = "", .photo_base64 = ""});

    contact_service_ = client_->Contacts();
  }

  void TearDown() override {
    contact_service_.reset();
    client_->Terminate();
    client_.reset();
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  transport::MockDataTransport* mock_dt_{};
  std::shared_ptr<Client> client_;
  std::shared_ptr<services::ContactService> contact_service_;

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

TEST_F(ContactServiceTest, CannotAddSelf) {
  auto const my_id = client_->GetId();

  auto const result = contact_service_->AddContact(my_id, std::nullopt);
  EXPECT_FALSE(result);

  auto const contact = contact_service_->GetContact(my_id);
  EXPECT_FALSE(contact.has_value());
}

TEST_F(ContactServiceTest, AddContact) {
  contact_service_->AddContact(peer1, std::nullopt);

  auto const contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::pendingApproval);

  // Verify system message was generated
  auto [messages, total_messages] = client_->GetMessageHistoryPage(TopicID(peer1), 0, 10);
  ASSERT_FALSE(messages.empty());
  auto const& last_msg = messages[0];
  EXPECT_EQ(last_msg.kind, messaging::HistoryEntryKind::systemMessage);
  EXPECT_EQ(last_msg.direction, messaging::MessageDirection::sent);
}

TEST_F(ContactServiceTest, ReceiveContactRequest) {
  bool request_cb_called = false;
  auto sub = EventQueue::Instance().Subscribe<EventContactRequest>([&](auto const& e) {
    if (e.peer_id == peer1) {
      request_cb_called = true;
    }
  });

  nlohmann::json profile;
  profile["display_name"] = "Peer One";

  contact_service_->OnContactAddRequest(peer1, std::nullopt, profile, {});

  EventQueue::Instance().WaitFor([&] { return request_cb_called; }, std::chrono::seconds(1));

  EventQueue::Instance().Unsubscribe(sub);
  EXPECT_TRUE(request_cb_called);
  auto const contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::awaitingApproval);
  EXPECT_EQ(contact->display_name, "Peer One");
}

TEST_F(ContactServiceTest, ConfirmContact) {
  // First receive a request
  nlohmann::json const profile;
  contact_service_->OnContactAddRequest(peer1, std::nullopt, profile, {});

  bool status_cb_called = false;
  auto sub = EventQueue::Instance().Subscribe<EventContactStatusChanged>([&](auto const& e) {
    if (e.peer_id == peer1 && e.status == identity::ContactStatus::confirmed) {
      status_cb_called = true;
    }
  });

  client_->Connect(peer1);
  if (auto p = mock_dt_->GetPeer(peer1)) {
    p->data_channel_is_open = true;
  }

  contact_service_->ConfirmContact(peer1);

  EventQueue::Instance().WaitFor([&] { return status_cb_called; }, std::chrono::seconds(1));

  EventQueue::Instance().Unsubscribe(sub);
  EXPECT_TRUE(status_cb_called);
  EXPECT_TRUE(contact_service_->IsConfirmed(peer1));

  bool signaling_sent = false;
  for (auto const& [p, json] : mock_dt_->SentJsonsSnapshot()) {
    if (p == peer1 && json.value("type", "") == "crcp_confirm") {
      signaling_sent = true;
      break;
    }
  }
  EXPECT_TRUE(signaling_sent);
}

TEST_F(ContactServiceTest, BlockContact) {
  contact_service_->AddContact(peer1, std::nullopt);

  contact_service_->BlockContact(peer1, true);

  auto contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::rejected);

  // Check if connections were closed - Media transport will receive Disconnect
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  bool closed = false;
  for (auto const& [pid, ctx] : mock_log_->closed_connections) {
    if (pid == peer1) {
      closed = true;
    }
  }
  EXPECT_TRUE(closed);
}

TEST_F(ContactServiceTest, UnblockContact) {
  contact_service_->AddContact(peer1, std::nullopt);
  contact_service_->BlockContact(peer1, false);

  contact_service_->UnblockContact(peer1);

  auto contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  // Should revert to awaiting approval or similar non-blocked state
  EXPECT_NE(contact->status, identity::ContactStatus::rejected);
}

// --- Security regression tests (audit C2): crcp_confirm must not bypass approval ---

TEST_F(ContactServiceTest, CrcpConfirmFromUnknownPeerDoesNotConfirm) {
  std::string mnemonic;
  auto const stranger = crypto::Identity::CreateNew(mnemonic);
  ContactID const stranger_id(stranger->GetPublicKey());

  nlohmann::json const msg = {
      {"type", "crcp_confirm"},
      {"signature", crypto::ToHex(stranger->Sign(client_->GetId().Span()))},
      {"profile", {{"display_name", "Stranger"}}}
  };
  contact_service_->ProcessCrcpMessage(stranger_id, stranger_id, msg);

  EXPECT_FALSE(contact_service_->IsConfirmed(stranger_id));
  auto const contact = contact_service_->GetContact(stranger_id);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::awaitingApproval);
}

TEST_F(ContactServiceTest, CrcpConfirmForOurPendingRequestConfirms) {
  std::string mnemonic;
  auto const friend_ident = crypto::Identity::CreateNew(mnemonic);
  ContactID const friend_id(friend_ident->GetPublicKey());

  contact_service_->AddContact(friend_id, std::nullopt);
  ASSERT_EQ(contact_service_->GetContact(friend_id)->status, identity::ContactStatus::pendingApproval);

  nlohmann::json const msg = {
      {"type", "crcp_confirm"},
      {"signature", crypto::ToHex(friend_ident->Sign(client_->GetId().Span()))},
      {"profile", nlohmann::json::object()}
  };
  contact_service_->ProcessCrcpMessage(friend_id, friend_id, msg);

  EXPECT_TRUE(contact_service_->IsConfirmed(friend_id));
}

TEST_F(ContactServiceTest, CrcpConfirmFromBlockedPeerDoesNotConfirm) {
  std::string mnemonic;
  auto const ident = crypto::Identity::CreateNew(mnemonic);
  ContactID const id(ident->GetPublicKey());

  contact_service_->AddContact(id, std::nullopt);
  contact_service_->BlockContact(id, false);

  nlohmann::json const msg = {
      {"type", "crcp_confirm"},
      {"signature", crypto::ToHex(ident->Sign(client_->GetId().Span()))},
      {"profile", nlohmann::json::object()}
  };
  contact_service_->ProcessCrcpMessage(id, id, msg);

  EXPECT_FALSE(contact_service_->IsConfirmed(id));
}

// --- P0-2b: auto-confirm only with proof of our prior approval (our signature over the peer's ID) ---

namespace {
auto ConfirmMsg(crypto::Identity const& from, ContactID const& me, std::optional<std::vector<std::byte>> proof)
    -> nlohmann::json {
  nlohmann::json msg = {
      {"type", "crcp_confirm"},
      {"signature", crypto::ToHex(from.Sign(me.Span()))},
      {"profile", nlohmann::json::object()}
  };
  if (proof) {
    msg["proof"] = crypto::ToHex(*proof);
  }
  return msg;
}
}  // namespace

TEST_F(ContactServiceTest, CrcpConfirmFromUnknownPeerWithOurProofAutoConfirms) {
  std::string mnemonic;
  auto const old_friend = crypto::Identity::CreateNew(mnemonic);
  ContactID const friend_id(old_friend->GetPublicKey());

  // We signed the friend's ID when we approved them in the past (before losing our contact list).
  auto const proof = client_->Sign(friend_id.Span());
  contact_service_->ProcessCrcpMessage(friend_id, friend_id, ConfirmMsg(*old_friend, client_->GetId(), proof));

  EXPECT_TRUE(contact_service_->IsConfirmed(friend_id));
}

TEST_F(ContactServiceTest, CrcpConfirmWithProofSignedByOtherKeyDoesNotConfirm) {
  std::string mnemonic;
  auto const stranger = crypto::Identity::CreateNew(mnemonic);
  ContactID const stranger_id(stranger->GetPublicKey());

  auto const forged = stranger->Sign(stranger_id.Span());
  contact_service_->ProcessCrcpMessage(stranger_id, stranger_id, ConfirmMsg(*stranger, client_->GetId(), forged));

  EXPECT_FALSE(contact_service_->IsConfirmed(stranger_id));
  ASSERT_TRUE(contact_service_->GetContact(stranger_id).has_value());
  EXPECT_EQ(contact_service_->GetContact(stranger_id)->status, identity::ContactStatus::awaitingApproval);
}

TEST_F(ContactServiceTest, CrcpConfirmWithOurProofForDifferentPeerDoesNotConfirm) {
  std::string mnemonic;
  auto const real_friend = crypto::Identity::CreateNew(mnemonic);
  auto const attacker = crypto::Identity::CreateNew(mnemonic);
  ContactID const friend_id(real_friend->GetPublicKey());
  ContactID const attacker_id(attacker->GetPublicKey());

  // A genuine proof we issued to someone else must not work for the attacker.
  auto const proof_for_friend = client_->Sign(friend_id.Span());
  contact_service_->ProcessCrcpMessage(
      attacker_id,
      attacker_id,
      ConfirmMsg(*attacker, client_->GetId(), proof_for_friend)
  );

  EXPECT_FALSE(contact_service_->IsConfirmed(attacker_id));
}

TEST_F(ContactServiceTest, ConfirmMessageCarriesProofFromPeer) {
  std::string mnemonic;
  auto const friend_ident = crypto::Identity::CreateNew(mnemonic);
  ContactID const friend_id(friend_ident->GetPublicKey());

  contact_service_->AddContact(friend_id, std::nullopt);
  auto const their_sig = friend_ident->Sign(client_->GetId().Span());
  contact_service_->ProcessCrcpMessage(friend_id, friend_id, ConfirmMsg(*friend_ident, client_->GetId(), std::nullopt));
  ASSERT_TRUE(contact_service_->IsConfirmed(friend_id));

  mock_dt_->MarkVerified(friend_id);
  mock_dt_->GetPeer(friend_id)->data_channel_is_open = true;
  mock_dt_->ClearSent();
  contact_service_->ConfirmContact(friend_id);

  auto const sent = mock_dt_->SentJsonsSnapshot();
  auto const it = std::ranges::find_if(sent, [&](auto const& p) {
    return p.first == friend_id && p.second.value("type", "") == "crcp_confirm";
  });
  ASSERT_NE(it, sent.end());
  ASSERT_TRUE(it->second.contains("proof"));
  // The proof we echo is the friend's signature over our ID - which the friend can verify with its own key.
  EXPECT_TRUE(crypto::Identity::Verify(
      friend_id.Span(),
      client_->GetId().Span(),
      crypto::FromHex(it->second["proof"].get<std::string>())
  ));
  EXPECT_EQ(crypto::FromHex(it->second["proof"].get<std::string>()), their_sig);
}
