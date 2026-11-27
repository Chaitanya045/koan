// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_ASKPASS_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_ASKPASS_H_

#include <string>
#include <string_view>

#include "base/functional/callback.h"

// ssh's questions (passwords, key passphrases, 2FA codes, unknown host keys)
// reach Koan through SSH_ASKPASS: ssh runs a helper with the prompt as argv[1]
// and reads the answer from its stdout (readpass.c ssh_askpass()). Koan's
// helper is a tiny sh script in a per-tunnel 0700 directory that hands the
// prompt to the browser over FIFOs (koan_ssh_askpass_server.h); this file is
// the protocol and the UI's interface. Pure. The design and its threat model:
// .agent/notes/upstream/remote-spaces.md.
namespace koan::remote {

enum class SshPromptKind {
  // A password, key passphrase, PIN or keyboard-interactive answer (2FA). ssh
  // doesn't tell the helper whether a keyboard-interactive answer may echo,
  // so every one is masked.
  kSecret,
  // A yes/no question from ssh itself: ask_permission()
  // (SSH_ASKPASS_PROMPT=confirm), "Accept updated hostkeys?", a host key
  // that differs from the one known for its IP address.
  kConfirm,
  // An unknown host's key: "The authenticity of host ... can't be
  // established" with its fingerprint.
  kHostKey,
  // A notice with no answer (SSH_ASKPASS_PROMPT=none), e.g. "Confirm user
  // presence for key ..." while a security key waits for a touch. It ends
  // when the tunnel moves on (another prompt, connected, failed).
  kNotice,
};

struct SshPrompt {
  SshPromptKind kind = SshPromptKind::kSecret;
  // ssh's prompt, sanitized (SanitizeSshText()) but untrusted: a
  // keyboard-interactive prompt is the server's text ("(user@host) " comes
  // first, from ssh). The UI shows it as quoted text, never as markup.
  std::string text;
  // kHostKey: the key's fingerprint as ssh printed it ("SHA256:..."), for
  // the UI to show apart from the text. Empty if not found.
  std::string fingerprint;

  bool operator==(const SshPrompt&) const = default;
};

// A secret (password, passphrase, code) on its way from the UI to ssh. Not
// copyable; the bytes are wiped (OPENSSL_cleanse) when it's destroyed, moved
// from or cleared. Never log it.
class SshSecret {
 public:
  SshSecret();
  explicit SshSecret(std::string value);
  SshSecret(SshSecret&& other);
  SshSecret& operator=(SshSecret&& other);
  SshSecret(const SshSecret&) = delete;
  SshSecret& operator=(const SshSecret&) = delete;
  ~SshSecret();

  std::string_view value() const { return value_; }
  bool empty() const { return value_.empty(); }
  void Clear();

 private:
  std::string value_;
};

// The UI's answer to a prompt.
struct SshPromptReply {
  // kSecret: OK with the secret; kConfirm / kHostKey: yes. False is "no" for
  // a question and "cancel" for a secret (which kills ssh: the tunnel attempt
  // ends, and ssh gets no answer at all).
  bool accepted = false;
  SshSecret secret;  // kSecret only.

  static SshPromptReply Cancel();
  static SshPromptReply Yes();
  static SshPromptReply No();
  static SshPromptReply WithSecret(SshSecret secret);
};

using SshPromptReplyCallback = base::OnceCallback<void(SshPromptReply)>;

// The UI for ssh's prompts (a Koan dialog, PR 3). One per tunnel, called on
// the tunnel's sequence. Each prompt has an id unique for the tunnel's
// lifetime.
class SshPromptDelegate {
 public:
  virtual ~SshPromptDelegate() = default;

  // Shows `prompt` for the tunnel to `destination` (the user's own text, so
  // trusted: use it for the dialog's title). Run `reply` at most once; it may
  // be dropped. For kNotice, `reply` is null.
  virtual void ShowSshPrompt(int id,
                             std::string_view destination,
                             const SshPrompt& prompt,
                             SshPromptReplyCallback reply) = 0;
  // The prompt isn't needed anymore (ssh moved on or exited, the user
  // disconnected): close it without running its reply.
  virtual void CancelSshPrompt(int id) = 0;
};

// The longest prompt Koan shows, and the longest answer ssh reads (its buffer
// is 1024 bytes with the NUL, readpass.c).
inline constexpr size_t kMaxSshPromptBytes = 8 * 1024;
inline constexpr size_t kMaxSshAnswerBytes = 1023;

// A request file the helper wrote: SSH_ASKPASS_PROMPT ("", "confirm" or
// "none"), '\n', then the prompt to the end of the file.
SshPrompt ParseAskpassRequest(std::string_view request);

// Whether ssh can take `secret` as an answer: not empty, at most
// kMaxSshAnswerBytes, and no line break or NUL (ssh would cut it short).
bool IsValidSshSecret(std::string_view secret);

// What the browser writes to the helper's answer FIFO for `reply` to a
// `kind` prompt: "ok\n<answer>\n", where the helper prints <answer> for ssh.
// A host key or confirm question is answered with an explicit "yes" or "no":
// an empty answer means yes to ask_permission(). Returned as a secret, since
// it may hold one.
//
// Empty means "write nothing": a cancelled secret (or one that isn't
// IsValidSshSecret()), and a notice. A secret prompt is never refused through
// the helper: a failed askpass makes read_passphrase() return "", and ssh
// sends that as the password or code (readpass.c), which the server counts as
// a failed login. Whoever cancels a secret kills ssh instead, and the helper
// dies with it.
SshSecret EncodeAskpassAnswer(SshPromptKind kind, const SshPromptReply& reply);

// The helper script (POSIX sh). ssh runs it as `<dir>/askpass <prompt>`;
// it creates <dir>/a.<pid> (its answer FIFO, opened read-write so opening
// never blocks), writes <dir>/q.<pid> (the request), writes "<pid>\n" to
// <dir>/ctl (the browser's FIFO), and prints the answer's second line.
std::string_view AskpassHelperScript();

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_ASKPASS_H_
