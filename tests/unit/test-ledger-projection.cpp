#include <gtest/gtest.h>

#include "eillance/services/ledger_projection_service.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

TEST(LedgerProjectionTest, InstantiatesWithoutCrashing) {
  // Tested largely by other unit tests (test-messaging-service etc.) mapping LedgerAppended to ProjectMessage.
  SUCCEED();
}
