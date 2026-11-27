// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_askpass.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_util.h"
#include "koan/browser/remote/koan_ssh_text.h"
#include "third_party/boringssl/src/include/openssl/mem.h"

namespace koan::remote {

namespace {

void Wipe(std::string& value) {
  if (!value.empty()) {
    OPENSSL_cleanse(value.data(), value.size());
  }
  value.clear();
}

// sshconnect.c check_host_key(), for a host not in known_hosts. The prompt is
// built by ssh itself, so a server can't start one this way: its
// keyboard-interactive prompts reach the helper as "(user@host) <prompt>"
// (sshconnect2.c input_userauth_info_req()).
constexpr std::string_view kUnknownHostPrefix = "The authenticity of host '";
constexpr std::string_view kFingerprintMarker = " key fingerprint is";

// "ED25519 key fingerprint is SHA256:<base64>." (OpenSSH 9) or "... is:
// SHA256:<base64>" (OpenSSH 10): the token after the marker, without a final
// period (base64 and MD5's hex pairs have none).
std::string FindFingerprint(std::string_view prompt) {
  const size_t marker = prompt.find(kFingerprintMarker);
  if (marker == std::string_view::npos) {
    return std::string();
  }
  std::string_view rest = prompt.substr(marker + kFingerprintMarker.size());
  if (rest.starts_with(':')) {
    rest.remove_prefix(1);
  }
  rest = base::TrimWhitespaceASCII(rest, base::TRIM_LEADING);
  rest = rest.substr(0, rest.find_first_of(" \t\r\n"));
  if (rest.ends_with('.')) {
    rest.remove_suffix(1);
  }
  return std::string(rest);
}

bool IsYesNoQuestion(std::string_view prompt) {
  std::string_view trimmed =
      base::TrimWhitespaceASCII(prompt, base::TRIM_TRAILING);
  return !prompt.starts_with('(') &&
         (trimmed.ends_with("(yes/no)?") || trimmed.ends_with("(yes/no):"));
}

constexpr char kHelperScript[] =
    "#!/bin/sh\n"
    "# Koan's SSH_ASKPASS helper (koan/browser/remote/koan_ssh_askpass.h).\n"
    "# It never stores or prints an answer anywhere but stdout, ssh's pipe.\n"
    "PATH=/usr/bin:/bin\n"
    "umask 077\n"
    "d=${0%/*}\n"
    "a=$d/a.$$\n"
    "q=$d/q.$$\n"
    "trap 'rm -f \"$a\" \"$q\"' EXIT\n"
    "rm -f \"$a\"\n"
    "mkfifo \"$a\" || exit 1\n"
    // Read-write, so opening the FIFO never blocks.
    "exec 3<>\"$a\" || exit 1\n"
    "printf '%s\\n%s' \"${SSH_ASKPASS_PROMPT-}\" \"$1\" >\"$q\" || exit 1\n"
    "printf '%s\\n' \"$$\" >\"$d/ctl\" || exit 1\n"
    // The status line only; the answer goes straight from the FIFO to ssh
    // and never into a shell variable.
    "IFS= read -r s <&3 || exit 1\n"
    "[ \"$s\" = ok ] || exit 1\n"
    "head -n 1 <&3\n";

}  // namespace

SshSecret::SshSecret() = default;

SshSecret::SshSecret(std::string value) : value_(std::move(value)) {}

SshSecret::SshSecret(SshSecret&& other) : value_(other.value_) {
  // A copy, then a wipe: moving a short std::string leaves its bytes behind.
  Wipe(other.value_);
}

SshSecret& SshSecret::operator=(SshSecret&& other) {
  if (this != &other) {
    Wipe(value_);
    value_ = other.value_;
    Wipe(other.value_);
  }
  return *this;
}

SshSecret::~SshSecret() {
  Wipe(value_);
}

void SshSecret::Clear() {
  Wipe(value_);
}

SshPromptReply SshPromptReply::Cancel() {
  return SshPromptReply();
}

SshPromptReply SshPromptReply::Yes() {
  SshPromptReply reply;
  reply.accepted = true;
  return reply;
}

SshPromptReply SshPromptReply::No() {
  return SshPromptReply();
}

SshPromptReply SshPromptReply::WithSecret(SshSecret secret) {
  SshPromptReply reply;
  reply.accepted = true;
  reply.secret = std::move(secret);
  return reply;
}

SshPrompt ParseAskpassRequest(std::string_view request) {
  std::string_view hint;
  std::string_view prompt = request;
  if (const size_t newline = request.find('\n');
      newline != std::string_view::npos) {
    hint = request.substr(0, newline);
    prompt = request.substr(newline + 1);
  }
  SshPrompt result;
  result.text = SanitizeSshText(prompt, kMaxSshPromptBytes,
                                /*keep_newlines=*/true);
  if (hint == "confirm") {
    result.kind = SshPromptKind::kConfirm;
  } else if (hint == "none") {
    result.kind = SshPromptKind::kNotice;
  } else if (prompt.starts_with(kUnknownHostPrefix)) {
    result.kind = SshPromptKind::kHostKey;
    result.fingerprint = SanitizeSshText(FindFingerprint(prompt), 128,
                                         /*keep_newlines=*/false);
  } else if (IsYesNoQuestion(prompt)) {
    result.kind = SshPromptKind::kConfirm;
  } else {
    result.kind = SshPromptKind::kSecret;
  }
  return result;
}

bool IsValidSshSecret(std::string_view secret) {
  return !secret.empty() && secret.size() <= kMaxSshAnswerBytes &&
         std::ranges::none_of(secret, [](char c) {
           return c == '\r' || c == '\n' || c == '\0';
         });
}

SshSecret EncodeAskpassAnswer(SshPromptKind kind, const SshPromptReply& reply) {
  std::string answer;
  answer.reserve(kMaxSshAnswerBytes + 8);
  switch (kind) {
    case SshPromptKind::kSecret:
      if (!reply.accepted || !IsValidSshSecret(reply.secret.value())) {
        break;  // Nothing: see the header.
      }
      answer = "ok\n";
      answer.append(reply.secret.value());
      answer.push_back('\n');
      break;
    case SshPromptKind::kConfirm:
    case SshPromptKind::kHostKey:
      answer = reply.accepted ? "ok\nyes\n" : "ok\nno\n";
      break;
    case SshPromptKind::kNotice:
      break;  // ssh ignores a notice's output and kills its helper.
  }
  return SshSecret(std::move(answer));
}

std::string_view AskpassHelperScript() {
  return kHelperScript;
}

}  // namespace koan::remote
