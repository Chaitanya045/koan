// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SOCKET_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SOCKET_H_

#include <sys/types.h>

#include <optional>

#include "base/files/file_path.h"

// Where the gateway's unix socket lives, and the checks that keep it safe
// (.agent/notes/agent-control.md section 4.1, "Socket hygiene"): bind() leaves
// a stale file behind and a mode of 0777 minus the umask, sun_path is short,
// and a directory or a socket that someone else made must not be trusted.
namespace koan::agent {

// The longest socket path that fits in sun_path on every platform (macOS: 104
// bytes with the terminator).
inline constexpr size_t kMaxSocketPathLength = 103;

// <profile>/AgentControl/agent.sock, or if that is too long for sun_path
// (a deep --user-data-dir), <tmp_root>/koan-<uid>/<16 hex of the SHA-256 of the
// profile path>.sock. nullopt if even that does not fit.
std::optional<base::FilePath> ChooseSocketPath(
    const base::FilePath& profile_dir,
    uid_t uid,
    const base::FilePath& tmp_root = base::FilePath("/tmp"));

enum class SocketPrepareResult {
  kOk,
  // The directory is a symlink, not a directory, or someone else's.
  kDirectoryUnsafe,
  // Something that is not a socket of ours is at the path.
  kNotOurs,
  // A browser is listening on it already.
  kInUse,
  kError,
};

// Makes the directory of `socket_path` (0700, owned by `uid`, not a symlink;
// one that is ours but looser is tightened), and removes a dead socket at the
// path (a connect() is refused) so that bind() can succeed. Refuses to touch
// anything else.
SocketPrepareResult PrepareSocketPath(const base::FilePath& socket_path,
                                      uid_t uid);

// After bind(): 0600, and checks that it is a socket owned by `uid`.
bool RestrictSocketPermissions(const base::FilePath& socket_path, uid_t uid);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_SOCKET_H_
