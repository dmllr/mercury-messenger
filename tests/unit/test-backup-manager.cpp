#include "eillance/backup/backup_manager.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

class BackupManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create a unique temporary directory for each test
    const ::testing::TestInfo* const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    temp_dir_ = std::filesystem::temp_directory_path() / "backup_manager_tests" / (std::string(test_info->test_suite_name()) + "_" + std::string(test_info->name()));
    std::filesystem::create_directories(temp_dir_);

    db_path_ = temp_dir_ / "test.db";
    backup_path_ = temp_dir_ / "test.backup";

    // Create a dummy database file with some content
    std::ofstream db_file(db_path_, std::ios::binary);
    original_db_content_ = "This is the original database content.";
    db_file.write(original_db_content_.c_str(), original_db_content_.size());
    db_file.close();
  }

  void TearDown() override {
    // Clean up the temporary directory
    std::filesystem::remove_all(temp_dir_);
  }

  std::filesystem::path temp_dir_;
  std::filesystem::path db_path_;
  std::filesystem::path backup_path_;
  std::string original_db_content_;
  const std::string password_ = "correct_password";
};

TEST_F(BackupManagerTest, CreateBackupSuccess) {
  eillance::backup::BackupManager backup_manager(db_path_.string());

  ASSERT_TRUE(backup_manager.CreateBackup(backup_path_.string(), password_));
  EXPECT_TRUE(std::filesystem::exists(backup_path_));
  EXPECT_GT(std::filesystem::file_size(backup_path_), 0);
}

TEST_F(BackupManagerTest, CreateBackupEmptyPassword) {
  eillance::backup::BackupManager backup_manager(db_path_.string());
  ASSERT_FALSE(backup_manager.CreateBackup(backup_path_.string(), ""));
  EXPECT_FALSE(std::filesystem::exists(backup_path_));
}

TEST_F(BackupManagerTest, RestoreFromBackupSuccess) {
  eillance::backup::BackupManager backup_manager(db_path_.string());
  ASSERT_TRUE(backup_manager.CreateBackup(backup_path_.string(), password_));

  // Modify the original DB file
  std::ofstream db_file(db_path_, std::ios::binary | std::ios::trunc);
  db_file << "Modified content";
  db_file.close();

  // Restore from backup
  ASSERT_TRUE(backup_manager.RestoreFromBackup(backup_path_.string(), password_));

  // Check if the content is restored
  std::ifstream restored_db_file(db_path_, std::ios::binary);
  std::string restored_content((std::istreambuf_iterator<char>(restored_db_file)), std::istreambuf_iterator<char>());
  EXPECT_EQ(restored_content, original_db_content_);
}

TEST_F(BackupManagerTest, RestoreFromBackupWrongPassword) {
  eillance::backup::BackupManager backup_manager(db_path_.string());
  ASSERT_TRUE(backup_manager.CreateBackup(backup_path_.string(), password_));

  // Modify the original DB file
  std::string modified_content = "Modified content";
  std::ofstream db_file(db_path_, std::ios::binary | std::ios::trunc);
  db_file << modified_content;
  db_file.close();

  // Attempt to restore with wrong password
  ASSERT_FALSE(backup_manager.RestoreFromBackup(backup_path_.string(), "wrong_password"));

  // Check that the DB file was not changed
  std::ifstream not_restored_db_file(db_path_, std::ios::binary);
  std::string current_content((std::istreambuf_iterator<char>(not_restored_db_file)), std::istreambuf_iterator<char>());
  EXPECT_EQ(current_content, modified_content);
}

TEST_F(BackupManagerTest, RestoreFromBackupCorruptedFile) {
  eillance::backup::BackupManager backup_manager(db_path_.string());
  ASSERT_TRUE(backup_manager.CreateBackup(backup_path_.string(), password_));

  // Corrupt the backup file (truncate it)
  std::filesystem::resize_file(backup_path_, std::filesystem::file_size(backup_path_) / 2);

  // Modify the original DB file
  std::string modified_content = "Modified content";
  std::ofstream db_file(db_path_, std::ios::binary | std::ios::trunc);
  db_file << modified_content;
  db_file.close();

  // Attempt to restore from corrupted backup
  ASSERT_FALSE(backup_manager.RestoreFromBackup(backup_path_.string(), password_));

  // Check that the DB file was not changed
  std::ifstream not_restored_db_file(db_path_, std::ios::binary);
  std::string current_content((std::istreambuf_iterator<char>(not_restored_db_file)), std::istreambuf_iterator<char>());
  EXPECT_EQ(current_content, modified_content);
}

TEST_F(BackupManagerTest, RestoreToNonExistentDBPath) {
  // This scenario simulates restoring to a new device where the DB file doesn't exist yet.
  std::filesystem::path new_db_path = temp_dir_ / "new.db";
  eillance::backup::BackupManager original_backup_manager(db_path_.string());
  ASSERT_TRUE(original_backup_manager.CreateBackup(backup_path_.string(), password_));

  // Ensure the new DB path doesn't exist
  if (std::filesystem::exists(new_db_path)) {
    std::filesystem::remove(new_db_path);
  }

  eillance::backup::BackupManager restore_manager(new_db_path.string());
  ASSERT_TRUE(restore_manager.RestoreFromBackup(backup_path_.string(), password_));

  // Check if the new DB file was created and has the correct content
  EXPECT_TRUE(std::filesystem::exists(new_db_path));
  std::ifstream restored_db_file(new_db_path, std::ios::binary);
  std::string restored_content((std::istreambuf_iterator<char>(restored_db_file)), std::istreambuf_iterator<char>());
  EXPECT_EQ(restored_content, original_db_content_);
}
