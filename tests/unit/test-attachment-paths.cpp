#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "eillance/messaging/attachment_paths.hpp"

using namespace eillance::messaging;

TEST(AttachmentPaths, ValidIds) {
  EXPECT_TRUE(IsValidAttachmentId("a1b2c3d4e5f60718293a4b5c6d7e8f90"));
  EXPECT_TRUE(IsValidAttachmentId("att_1"));
  EXPECT_TRUE(IsValidAttachmentId("resume-test-id"));
}

TEST(AttachmentPaths, InvalidIds) {
  EXPECT_FALSE(IsValidAttachmentId(""));
  EXPECT_FALSE(IsValidAttachmentId("../evil"));
  EXPECT_FALSE(IsValidAttachmentId("a/b"));
  EXPECT_FALSE(IsValidAttachmentId("a\b"));
  EXPECT_FALSE(IsValidAttachmentId("/etc/passwd"));
  EXPECT_FALSE(IsValidAttachmentId("a.b"));
  EXPECT_FALSE(IsValidAttachmentId(std::string(maxAttachmentIdLength + 1, 'a')));
  EXPECT_FALSE(IsValidAttachmentId(std::string("a\0b", 3)));
}

TEST(AttachmentPaths, SanitizeKeepsNormalNames) {
  EXPECT_EQ(SanitizeAttachmentFileName("photo.jpg"), "photo.jpg");
  EXPECT_EQ(SanitizeAttachmentFileName("My Report (final).pdf"), "My Report (final).pdf");
  EXPECT_EQ(SanitizeAttachmentFileName("Привет.txt"), "Привет.txt");
}

TEST(AttachmentPaths, SanitizeStripsTraversal) {
  EXPECT_EQ(SanitizeAttachmentFileName("../../.ssh/authorized_keys"), "authorized_keys");
  EXPECT_EQ(SanitizeAttachmentFileName(R"(..\..\evil.dll)"), "evil.dll");
  EXPECT_EQ(SanitizeAttachmentFileName("/etc/passwd"), "passwd");
  EXPECT_EQ(SanitizeAttachmentFileName(R"(C:\Windows\win.ini)"), "win.ini");
  EXPECT_EQ(SanitizeAttachmentFileName(".."), "");
  EXPECT_EQ(SanitizeAttachmentFileName("."), "");
  EXPECT_EQ(SanitizeAttachmentFileName("dir/.."), "");
  EXPECT_EQ(SanitizeAttachmentFileName(""), "");
  EXPECT_EQ(SanitizeAttachmentFileName(".bashrc"), "bashrc");
}

TEST(AttachmentPaths, SanitizeRemovesControlAndReservedChars) {
  EXPECT_EQ(SanitizeAttachmentFileName(std::string("a\nb\0c.txt", 9)), "abc.txt");
  EXPECT_EQ(SanitizeAttachmentFileName("a:b*c?.txt"), "a_b_c_.txt");
  EXPECT_EQ(SanitizeAttachmentFileName("CON.txt"), "_CON.txt");
  EXPECT_EQ(SanitizeAttachmentFileName("com1"), "_com1");
}

TEST(AttachmentPaths, SanitizeBoundsLength) {
  auto const out = SanitizeAttachmentFileName(std::string(1000, 'x') + ".png");
  EXPECT_LE(out.size(), maxAttachmentFileNameLength);
  EXPECT_TRUE(out.ends_with(".png"));
}

TEST(AttachmentPaths, InsideDirectory) {
  std::filesystem::path const dir = std::filesystem::temp_directory_path() / "att_paths_test" / "attachments";
  EXPECT_TRUE(IsPathInsideDirectory(dir, dir / "file.txt"));
  EXPECT_FALSE(IsPathInsideDirectory(dir, dir));
  EXPECT_FALSE(IsPathInsideDirectory(dir, dir / ".." / "file.txt"));
  EXPECT_FALSE(IsPathInsideDirectory(dir, dir.parent_path() / "attachments_evil" / "f"));
  EXPECT_FALSE(IsPathInsideDirectory(dir, std::filesystem::temp_directory_path() / "x"));
  EXPECT_FALSE(IsPathInsideDirectory(dir, "relative.dat"));
}
