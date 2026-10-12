#include <chrono>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class ClientCoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();

    auto media_transport_impl = std::make_unique<transport::MockMediaTransport>(mock_log_);
    auto media_transport = std::make_shared<transport::MediaTransport>(std::move(media_transport_impl));

    auto data_transport = std::make_shared<transport::DataTransport>(std::make_unique<transport::MockDataTransport>());
    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);

    (void)client_->GenerateNewIdentity();
    my_id_ = client_->GetId();

    std::string mnemonic;
    peer_ident_ = eillance::crypto::Identity::CreateNew(mnemonic);
    peer1 = ContactID(peer_ident_->GetPublicKey());

    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;
  ContactID my_id_;

  std::unique_ptr<crypto::Identity> peer_ident_;
  ContactID peer1;
};

TEST_F(ClientCoreTest, ConnectSequence) {
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();

  client_->Connect(peer1);
  eillance::EventQueue::Instance().WaitFor(
      [&]() { return client_->GetPeerConnectionState(peer1) == transport::PeerConnectionState::connecting; },
      std::chrono::milliseconds(2000)
  );
  EXPECT_EQ(client_->GetPeerConnectionState(peer1), transport::PeerConnectionState::connecting);
}

TEST_F(ClientCoreTest, IncomingKeyExchangeTriggersReply) {
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();
  client_->Connect(peer1);

  // Setup sub to track queued messages trigger
  bool queued_messages_triggered = false;
  auto sub = EventQueue::Instance().Subscribe<EventSendQueuedMessages>([&](auto const& e) {
    if (e.peer_id == peer1) queued_messages_triggered = true;
  });

  eillance::crypto::SessionCipher dummy_cipher;
  auto test_sig = peer_ident_->Sign(client_->GetId().Span());
  nlohmann::json const key_msg = {
      {"type", "key_exchange"},
      {"key", std::vector<std::byte>()},
      {"is_reply", false},
      {"signature", crypto::ToHex(test_sig)}
  };

  // Transform the byte array to std::uint8 array to avoid nlohmann::json dump
  // implicitly mangling the std::byte arrays when parsed back via string parsing.
  nlohmann::json clean_msg = key_msg;
  clean_msg["key"] = nlohmann::json::array();
  for (auto b : dummy_cipher.GetPublicKey()) {
    clean_msg["key"].push_back(static_cast<uint8_t>(b));
  }
  std::string msg_str = clean_msg.dump();
  std::vector<std::byte> msg_bytes(msg_str.size());
  std::transform(msg_str.begin(), msg_str.end(), msg_bytes.begin(), [](char c) { return static_cast<std::byte>(c); });

  eillance::EventQueue::Instance().Push(eillance::EventDataChannelMessage{peer1, msg_bytes});

  // Once the message is parsed, it triggers EventSendQueuedMessages
  eillance::EventQueue::Instance().WaitFor(
      [&]() { return queued_messages_triggered; },
      std::chrono::milliseconds(2000)
  );

  EXPECT_TRUE(queued_messages_triggered);
  EventQueue::Instance().Unsubscribe(sub);
}

TEST_F(ClientCoreTest, SignalingMessageProcessing_MediaOffer) {
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();

  nlohmann::json const offer_msg =
      {{"from", peer1.ToString()}, {"type", "offer"}, {"context", "media"}, {"sdp", "dummy_sdp_media_offer_m=audio"}};

  std::string msg_str = offer_msg.dump();
  std::vector<std::byte> msg_bytes(msg_str.size());
  std::transform(msg_str.begin(), msg_str.end(), msg_bytes.begin(), [](char c) { return static_cast<std::byte>(c); });

  eillance::EventQueue::Instance().Push(eillance::EventDataChannelMessage{peer1, msg_bytes});
  eillance::EventQueue::Instance().WaitFor(
      [&]() { return client_->GetPeerConnectionState(peer1) == transport::PeerConnectionState::connecting; },
      std::chrono::milliseconds(1000)
  );

  SUCCEED() << " Test passes if it didn't crash parsing an incoming media SDP signaling payload";
}

TEST_F(ClientCoreTest, ContactRequestProcessing) {
  nlohmann::json const req_msg = {{"type", "contact_add_request"}, {"profile", {{"display_name", "Peer One"}}}};

  messaging::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  entry.type = messaging::LedgerActionType::ContactState;
  entry.payload = req_msg.dump();

  eillance::EventQueue::Instance().Push(EventLedgerAppended{entry});

  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto const contacts = client_->GetContacts();
        return !contacts.empty() && contacts[0].peer_id == peer1;
      },
      std::chrono::milliseconds(2000)
  );

  auto contacts = client_->GetContacts();
  ASSERT_EQ(contacts.size(), 1);
  EXPECT_EQ(contacts[0].peer_id, peer1);
  EXPECT_EQ(contacts[0].display_name, "Peer One");
  EXPECT_EQ(contacts[0].status, identity::ContactStatus::awaitingApproval);
}

TEST_F(ClientCoreTest, GroupLifecycle) {
  auto gid = client_->CreateGroup("My Group", {peer1});

  auto groups = client_->GetGroups();
  ASSERT_EQ(groups.size(), 1);
  EXPECT_EQ(groups[0].name, "My Group");

  auto members = client_->GetGroupMembers(gid);
  ASSERT_EQ(members.size(), 2);  // owner + peer1

  EXPECT_TRUE(
      client_->AddGroupMember(gid, ContactID("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15"))
  );
  members = client_->GetGroupMembers(gid);
  EXPECT_EQ(members.size(), 3);

  EXPECT_TRUE(client_->RemoveGroupMember(gid, peer1));
  members = client_->GetGroupMembers(gid);
  EXPECT_EQ(members.size(), 2);

  client_->LeaveGroup(gid);
  members = client_->GetGroupMembers(gid);
  EXPECT_EQ(members.size(), 1);
}

TEST_F(ClientCoreTest, ChannelLifecycle) {
  auto cid = client_->CreateChannel("My Channel", "Description");

  auto channels = client_->GetChannels();
  ASSERT_EQ(channels.size(), 1);
  EXPECT_EQ(channels[0].name, "My Channel");

  auto members = client_->GetChannelMembers(cid);
  ASSERT_EQ(members.size(), 1);

  EXPECT_TRUE(
      client_->AddChannelMember(cid, ContactID("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15"))
  );
  members = client_->GetChannelMembers(cid);
  EXPECT_EQ(members.size(), 2);

  EXPECT_TRUE(
      client_->RemoveChannelMember(cid, ContactID("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15"))
  );
  members = client_->GetChannelMembers(cid);
  EXPECT_EQ(members.size(), 1);

  client_->UnsubscribeChannel(cid);
  members = client_->GetChannelMembers(cid);
  EXPECT_EQ(members.size(), 0);
}
