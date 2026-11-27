// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_askpass.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::remote {
namespace {

// sshconnect.c's prompts, as OpenSSH 9.9 and 10.x print them.
constexpr char kUnknownHost9[] =
    "\nThe authenticity of host 'box (192.0.2.10)' can't be established.\n"
    "ED25519 key fingerprint is SHA256:sfBOvLh25Fxt3+ekmi47txPjb6l5MDI7l1RU."
    "\nThis key is not known by any other names.\n"
    "Are you sure you want to continue connecting (yes/no/[fingerprint])? ";
constexpr char kUnknownHost10[] =
    "\nThe authenticity of host '[127.0.0.1]:22555 ([127.0.0.1]:22555)' can't "
    "be established.\n"
    "ED25519 key fingerprint is: SHA256:sfBOvLh25Fxt3+ekmi47txPjb6l5MDI7l1RU\n"
    "This key is not known by any other names.\n"
    "Are you sure you want to continue connecting (yes/no/[fingerprint])? ";

TEST(KoanSshAskpassTest, TellsPromptKindsApart) {
  SshPrompt prompt = ParseAskpassRequest(kUnknownHost9);
  EXPECT_EQ(prompt.kind, SshPromptKind::kHostKey);
  EXPECT_EQ(prompt.fingerprint, "SHA256:sfBOvLh25Fxt3+ekmi47txPjb6l5MDI7l1RU");

  prompt = ParseAskpassRequest(kUnknownHost10);
  EXPECT_EQ(prompt.kind, SshPromptKind::kHostKey);
  EXPECT_EQ(prompt.fingerprint, "SHA256:sfBOvLh25Fxt3+ekmi47txPjb6l5MDI7l1RU");

  EXPECT_EQ(ParseAskpassRequest("\nme@box's password: ").kind,
            SshPromptKind::kSecret);
  EXPECT_EQ(
      ParseAskpassRequest("\nEnter passphrase for key '/Users/me/key': ").kind,
      SshPromptKind::kSecret);
  EXPECT_EQ(ParseAskpassRequest("\n(me@box) Verification code: ").kind,
            SshPromptKind::kSecret);
  // A server's keyboard-interactive prompt can't pass for ssh's questions.
  prompt = ParseAskpassRequest(
      "\n(me@box) The authenticity of host 'x' can't be established.\n"
      "Are you sure you want to continue connecting (yes/no)? ");
  EXPECT_EQ(prompt.kind, SshPromptKind::kSecret);
  EXPECT_TRUE(prompt.fingerprint.empty());

  EXPECT_EQ(
      ParseAskpassRequest("confirm\nAllow use of key test_key_ed25519?").kind,
      SshPromptKind::kConfirm);
  EXPECT_EQ(ParseAskpassRequest("\nAccept updated hostkeys? (yes/no): ").kind,
            SshPromptKind::kConfirm);
  EXPECT_EQ(ParseAskpassRequest(
                "\nWarning: the ED25519 host key for 'box' differs from the "
                "key for the IP address '192.0.2.10'\nOffending key for IP in "
                "/Users/me/.ssh/known_hosts:3\nAre you sure you want to "
                "continue connecting (yes/no)? ")
                .kind,
            SshPromptKind::kConfirm);
  prompt = ParseAskpassRequest(
      "none\nConfirm user presence for key ED25519-SK SHA256:abc");
  EXPECT_EQ(prompt.kind, SshPromptKind::kNotice);
  EXPECT_EQ(prompt.text, "Confirm user presence for key ED25519-SK SHA256:abc");
}

TEST(KoanSshAskpassTest, PromptTextIsSanitized) {
  const SshPrompt prompt = ParseAskpassRequest(
      "\n(me@box) Code\x1b[2J\xE2\x80\xAE: \x07\nline two\t\xFF");
  EXPECT_EQ(prompt.text, "(me@box) Code[2J: \nline two \xEF\xBF\xBD");
  EXPECT_LE(ParseAskpassRequest("\n" + std::string(100000, 'x')).text.size(),
            kMaxSshPromptBytes);
}

TEST(KoanSshAskpassTest, EncodesAnswers) {
  EXPECT_EQ(
      EncodeAskpassAnswer(SshPromptKind::kSecret,
                          SshPromptReply::WithSecret(SshSecret("hunter2")))
          .value(),
      "ok\nhunter2\n");
  // A cancelled secret writes nothing: the helper failing would make ssh send
  // an empty password.
  EXPECT_TRUE(
      EncodeAskpassAnswer(SshPromptKind::kSecret, SshPromptReply::Cancel())
          .empty());
  // An answer ssh would cut short, or can't take, is no answer either.
  for (const std::string& bad :
       {std::string(), std::string("two\nlines"), std::string("cr\r"),
        std::string("nul\0", 4), std::string(kMaxSshAnswerBytes + 1, 'x')}) {
    EXPECT_FALSE(IsValidSshSecret(bad));
    EXPECT_TRUE(EncodeAskpassAnswer(SshPromptKind::kSecret,
                                    SshPromptReply::WithSecret(SshSecret(bad)))
                    .empty());
  }
  EXPECT_EQ(EncodeAskpassAnswer(SshPromptKind::kSecret,
                                SshPromptReply::WithSecret(SshSecret(
                                    std::string(kMaxSshAnswerBytes, 'x'))))
                .value()
                .size(),
            kMaxSshAnswerBytes + 4);
  // Questions get an explicit word: an empty answer would mean yes to
  // ask_permission().
  EXPECT_EQ(EncodeAskpassAnswer(SshPromptKind::kHostKey, SshPromptReply::Yes())
                .value(),
            "ok\nyes\n");
  EXPECT_EQ(EncodeAskpassAnswer(SshPromptKind::kHostKey, SshPromptReply::No())
                .value(),
            "ok\nno\n");
  EXPECT_EQ(EncodeAskpassAnswer(SshPromptKind::kConfirm, SshPromptReply::Yes())
                .value(),
            "ok\nyes\n");
  EXPECT_EQ(
      EncodeAskpassAnswer(SshPromptKind::kConfirm, SshPromptReply::Cancel())
          .value(),
      "ok\nno\n");
  EXPECT_TRUE(EncodeAskpassAnswer(SshPromptKind::kNotice, SshPromptReply::Yes())
                  .empty());
}

TEST(KoanSshAskpassTest, SecretsMoveAndClear) {
  SshSecret first("pw");
  SshSecret second(std::move(first));
  EXPECT_EQ(second.value(), "pw");
  EXPECT_TRUE(first.empty());  // NOLINT(bugprone-use-after-move)
  SshSecret third;
  third = std::move(second);
  EXPECT_EQ(third.value(), "pw");
  EXPECT_TRUE(second.empty());  // NOLINT(bugprone-use-after-move)
  third.Clear();
  EXPECT_TRUE(third.empty());
}

TEST(KoanSshAskpassTest, HelperScriptKeepsAnswersOutOfVariables) {
  const std::string_view script = AskpassHelperScript();
  EXPECT_TRUE(script.starts_with("#!/bin/sh\n"));
  // Only the status line is read into a variable; the answer streams to ssh.
  EXPECT_NE(script.find("IFS= read -r s <&3"), std::string_view::npos);
  EXPECT_NE(script.find("head -n 1 <&3"), std::string_view::npos);
  EXPECT_EQ(script.find("echo"), std::string_view::npos);
}

}  // namespace
}  // namespace koan::remote
