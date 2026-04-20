#pragma once

#include <atomic>
#include <memory>

namespace eillance {
namespace storage {
class PersistentStorage;
}
namespace transport {
class DataTransport;
class MediaTransport;
class Libp2pNode;
}  // namespace transport
namespace identity {
class ContactManager;
}
namespace sync {
class SyncEngine;
}
}  // namespace eillance

namespace eillance::services {

class AccountService;
class ContactService;
class FileTransferService;
class InternalMessagingService;
class MediaCallService;
class MessagingService;
class ProfileService;
class LedgerProjectionService;

struct ServiceManager {
  std::shared_ptr<storage::PersistentStorage> storage;
  std::shared_ptr<transport::DataTransport> data_transport;
  std::shared_ptr<transport::MediaTransport> media_transport;
  std::shared_ptr<identity::ContactManager> contact_manager;

  std::shared_ptr<AccountService> account;
  std::shared_ptr<FileTransferService> file_transfer;
  std::shared_ptr<MessagingService> messaging;
  std::shared_ptr<MediaCallService> media_call;
  std::shared_ptr<ProfileService> profile;
  std::shared_ptr<ContactService> contact;
  std::shared_ptr<InternalMessagingService> internal_messaging;
  std::shared_ptr<sync::SyncEngine> sync_engine;
  std::shared_ptr<LedgerProjectionService> projection_service;

  std::atomic<bool> is_foreground{false};
};

}  // namespace eillance::services
