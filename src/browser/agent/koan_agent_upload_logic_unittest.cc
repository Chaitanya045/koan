// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_upload_logic.h"

#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {
namespace {

TEST(KoanAgentUploadLogicTest, TheChooserSaysHowManyFiles) {
  EXPECT_FALSE(CheckChooser(ChooserMode::kOpen, 1));
  EXPECT_EQ(CheckChooser(ChooserMode::kOpen, 2)->code, "Koan.tooManyFiles");
  EXPECT_EQ(CheckChooser(ChooserMode::kOpen, 0)->code, "Koan.tooManyFiles");
  EXPECT_FALSE(CheckChooser(ChooserMode::kOpenMultiple, 1));
  EXPECT_FALSE(CheckChooser(ChooserMode::kOpenMultiple, kMaxUploadFiles));
  EXPECT_EQ(CheckChooser(ChooserMode::kOpenMultiple, kMaxUploadFiles + 1)->code,
            "Koan.tooManyFiles");
  // Folders are not supported yet; a save dialog is never an upload.
  EXPECT_EQ(CheckChooser(ChooserMode::kFolder, 1)->code, "Koan.notSupported");
  EXPECT_EQ(CheckChooser(ChooserMode::kSave, 1)->code, "Koan.notPermitted");
}

TEST(KoanAgentUploadLogicTest, AcceptByExtension) {
  const std::vector<std::string> pdf = {".pdf"};
  EXPECT_EQ(MatchAccept(pdf, "resume.pdf"), AcceptMatch::kMatches);
  EXPECT_EQ(MatchAccept(pdf, "Resume.PDF"), AcceptMatch::kMatches);
  EXPECT_EQ(MatchAccept(pdf, "resume.docx"), AcceptMatch::kMismatch);
  EXPECT_EQ(MatchAccept(pdf, "pdf"), AcceptMatch::kMismatch);
  EXPECT_EQ(MatchAccept(pdf, ".pdf"), AcceptMatch::kMismatch);
  // A longer extension.
  EXPECT_EQ(MatchAccept({".tar.gz"}, "logs.tar.gz"), AcceptMatch::kMatches);
  EXPECT_EQ(MatchAccept({".tar.gz"}, "logs.gz"), AcceptMatch::kMismatch);
  // Several entries: any one will do.
  EXPECT_EQ(MatchAccept({".doc", ".docx", ".pdf"}, "cv.docx"),
            AcceptMatch::kMatches);
}

TEST(KoanAgentUploadLogicTest, AcceptByMimeType) {
  EXPECT_EQ(MatchAccept({"image/*"}, "photo.png"), AcceptMatch::kMatches);
  EXPECT_EQ(MatchAccept({"image/*"}, "photo.JPG"), AcceptMatch::kMatches);
  EXPECT_EQ(MatchAccept({"image/png"}, "photo.jpg"), AcceptMatch::kMismatch);
  EXPECT_EQ(MatchAccept({"application/pdf"}, "resume.pdf"),
            AcceptMatch::kMatches);
  EXPECT_EQ(MatchAccept({"image/*"}, "resume.pdf"), AcceptMatch::kMismatch);
  // An extension Chrome knows no type for: maybe.
  EXPECT_EQ(MatchAccept({"image/*"}, "notes.koanfile"), AcceptMatch::kUnsure);
  EXPECT_EQ(MatchAccept({"image/*"}, "README"), AcceptMatch::kUnsure);
  // Mixed: the extension matches though the type doesn't.
  EXPECT_EQ(MatchAccept({"image/*", ".pdf"}, "resume.pdf"),
            AcceptMatch::kMatches);
}

TEST(KoanAgentUploadLogicTest, NoAcceptListTakesAnything) {
  EXPECT_EQ(MatchAccept({}, "anything.bin"), AcceptMatch::kMatches);
  // Entries Blink would not pass are not a list to check against.
  EXPECT_EQ(MatchAccept({"pdf"}, "anything.bin"), AcceptMatch::kMatches);
}

TEST(KoanAgentUploadLogicTest, CheckAcceptNamesTheFileAndTheList) {
  EXPECT_FALSE(CheckAccept({".pdf"}, {"a.pdf", "b.pdf"}));
  EXPECT_FALSE(CheckAccept({"image/*"}, {"maybe.koanfile"}));
  const std::optional<UploadRefusal> refused =
      CheckAccept({".pdf", "image/*"}, {"a.pdf", "b.docx"});
  ASSERT_TRUE(refused);
  EXPECT_EQ(refused->code, "Koan.notAccepted");
  EXPECT_NE(refused->message.find("b.docx"), std::string::npos);
  EXPECT_NE(refused->message.find(".pdf, image/*"), std::string::npos);
}

TEST(KoanAgentUploadLogicTest, PlainFileNames) {
  EXPECT_TRUE(IsPlainFileName("resume.pdf"));
  EXPECT_TRUE(IsPlainFileName("Lebenslauf – Ada.pdf"));
  EXPECT_FALSE(IsPlainFileName(""));
  EXPECT_FALSE(IsPlainFileName("."));
  EXPECT_FALSE(IsPlainFileName(".."));
  EXPECT_FALSE(IsPlainFileName("a/b.pdf"));
  EXPECT_FALSE(IsPlainFileName("a\\b.pdf"));
  EXPECT_FALSE(IsPlainFileName(std::string(256, 'a')));
  EXPECT_FALSE(IsPlainFileName("\xff\xfe.pdf"));
}

TEST(KoanAgentUploadLogicTest, RefusedFolders) {
  const std::vector<base::FilePath> roots = {
      base::FilePath("/Users/<user>/Library/Application Support/Koan"),
      base::FilePath("/Users/<user>/.gnupg")};
  EXPECT_TRUE(
      IsUnderAny(base::FilePath("/Users/<user>/.gnupg/pubring.kbx"), roots));
  EXPECT_TRUE(IsUnderAny(base::FilePath("/Users/<user>/.gnupg"), roots));
  EXPECT_TRUE(IsUnderAny(
      base::FilePath(
          "/Users/<user>/Library/Application Support/Koan/Default/Login Data"),
      roots));
  EXPECT_FALSE(
      IsUnderAny(base::FilePath("/Users/<user>/.gnupgkeys.pdf"), roots));
  EXPECT_FALSE(
      IsUnderAny(base::FilePath("/Users/<user>/Desktop/cv.pdf"), roots));
  EXPECT_FALSE(IsUnderAny(base::FilePath("/x"), {base::FilePath()}));
}

TEST(KoanAgentUploadLogicTest, WhatTheIndicatorAndTheAuditSay) {
  EXPECT_EQ(AttachedText({}), u"");
  EXPECT_EQ(AttachedText({"resume.pdf"}), u"resume.pdf");
  EXPECT_EQ(AttachedText({"resume.pdf", "cover.pdf"}),
            u"resume.pdf and cover.pdf");
  EXPECT_EQ(AttachedText({"a.png", "b.png", "c.png"}),
            u"a.png and 2 more files");

  EXPECT_EQ(SizeText(12), "12 B");
  EXPECT_EQ(SizeText(2048), "2.0 KB");
  EXPECT_EQ(SizeText(5 * 1024 * 1024 + 1024 * 100), "5.1 MB");
  EXPECT_EQ(SizeText(int64_t{3} * 1024 * 1024 * 1024), "3.0 GB");
  EXPECT_EQ(UploadSummary({"resume.pdf", "x.txt"}, {123289, 12}),
            "resume.pdf (120.4 KB), x.txt (12 B)");
}

}  // namespace
// Where no upload is taken from, under the home folder: the same list on the
// Mac as before, and Linux's own stores there (issue #489).
TEST(KoanAgentUploadLogicTest, CredentialStoresAreRefused) {
  const base::FilePath home("/home/example");
  const std::vector<base::FilePath> roots = RefusedHomeFolders(home);
  for (const char* path : {".ssh", ".gnupg/pubring.kbx", ".koan/clients/a/key",
                           ".aws/credentials"}) {
    EXPECT_TRUE(IsUnderAny(home.Append(path), roots)) << path;
  }
  EXPECT_FALSE(IsUnderAny(home.Append("Downloads/cv.pdf"), roots));
  EXPECT_FALSE(IsUnderAny(home.Append(".sshkeys.pdf"), roots));
  EXPECT_TRUE(RefusedHomeFolders(base::FilePath()).empty());
#if BUILDFLAG(IS_LINUX)
  for (const char* path : {
           ".local/share/keyrings/login.keyring",
           ".local/share/kwalletd/kdewallet.kwl",
           ".pki/nssdb/key4.db",
           ".mozilla/firefox/abc.default/logins.json",
           ".config/google-chrome/Default/Login Data",
           ".config/chromium/Default/Cookies",
           ".config/BraveSoftware/Brave-Browser/Default/Cookies",
           ".var/app/dev.koan.Koan/config/koan/Default/Cookies",
           ".var/app/org.mozilla.firefox/.mozilla/firefox/x/key4.db",
       }) {
    EXPECT_TRUE(IsUnderAny(home.Append(path), roots)) << path;
  }
  EXPECT_FALSE(IsUnderAny(home.Append(".local/share/Trash/x.pdf"), roots));
  EXPECT_FALSE(IsUnderAny(home.Append(".config/koan-notes.txt"), roots));
#else
  EXPECT_TRUE(
      IsUnderAny(home.Append("Library/Keychains/login.keychain-db"), roots));
  EXPECT_FALSE(IsUnderAny(home.Append(".var/app/x"), roots));
#endif
}

}  // namespace koan::agent
