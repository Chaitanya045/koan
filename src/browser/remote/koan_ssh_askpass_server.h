// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_ASKPASS_SERVER_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_ASKPASS_SERVER_H_

#include <sys/types.h>

#include <memory>
#include <string>

#include "base/containers/flat_map.h"
#include "base/files/file_descriptor_watcher_posix.h"
#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/functional/callback.h"
#include "base/sequence_checker.h"
#include "koan/browser/remote/koan_ssh_askpass.h"

namespace koan::remote {

// The browser's end of the askpass helper (koan_ssh_askpass.h), for one ssh
// run. It lives in a directory the owner created with mode 0700 and deletes
// afterwards: the helper script, "ctl" (a FIFO the browser reads helpers'
// "<pid>\n" from), and per request "q.<pid>" (the prompt, a 0600 file: no
// secrets) and "a.<pid>" (the answer FIFO, 0600). Answers go through the
// kernel's pipe buffer only, never to disk.
//
// Only a helper in the tunnel's process group (the supervisor's, set with
// SetProcessGroup()) is answered: other processes of the same user could
// write to ctl, and they get nothing. Other users can't reach the directory.
//
// A secret is never refused through the helper (EncodeAskpassAnswer()): a
// cancelled one, and every request still open when the server goes away, get
// no answer at all. The owner kills ssh's process group first, and the
// helpers die with it.
//
// Lives on one sequence that may block and can watch file descriptors (a
// MayBlock ThreadPool sequence or an IO thread).
class SshAskpassServer {
 public:
  // A request from helper `id` (its pid).
  using RequestCallback = base::RepeatingCallback<void(int id, SshPrompt)>;

  // Sets up `dir` (which must exist, be empty, be the user's and have mode
  // 0700). `on_bad_request` runs when a helper of the group sent a request
  // that can't be read: it gets no answer (it might be a password prompt),
  // so the owner must stop ssh. Null on failure.
  static std::unique_ptr<SshAskpassServer> Create(
      const base::FilePath& dir,
      RequestCallback on_request,
      base::RepeatingClosure on_bad_request);

  SshAskpassServer(const SshAskpassServer&) = delete;
  SshAskpassServer& operator=(const SshAskpassServer&) = delete;
  ~SshAskpassServer();

  // What SSH_ASKPASS points at.
  const base::FilePath& helper_path() const { return helper_path_; }

  // The supervisor's pid, which is its process group's id.
  void SetProcessGroup(pid_t process_group);

  // Answers request `id`; nothing if it's gone (the helper was killed), or if
  // EncodeAskpassAnswer() has nothing to write (a cancelled secret: the
  // caller kills ssh). The reply's secret is wiped when the caller drops
  // `reply`.
  void Reply(int id, const SshPromptReply& reply);

 private:
  SshAskpassServer(base::FilePath dir,
                   base::ScopedFD control,
                   RequestCallback on_request,
                   base::RepeatingClosure on_bad_request);

  void OnControlReadable();
  void HandleRequest(int id);
  void Answer(int id, SshPromptKind kind, const SshPromptReply& reply);

  const base::FilePath dir_;
  const base::FilePath helper_path_;
  base::ScopedFD control_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> control_watch_;
  RequestCallback on_request_;
  base::RepeatingClosure on_bad_request_;
  pid_t process_group_ = 0;
  // Bytes of an unfinished "<pid>\n".
  std::string partial_line_;
  // Open requests and their kinds.
  base::flat_map<int, SshPromptKind> open_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_ASKPASS_SERVER_H_
