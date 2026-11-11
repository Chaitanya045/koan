// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SOCKET_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SOCKET_H_

#include <sys/types.h>

#include <optional>

#include "base/files/file_path.h"
#include "base/files/scoped_file.h"

// Where the gateway's unix socket lives, and the checks that keep it safe
// (.agent/notes/agent-control.md section 4.1, "Socket hygiene"): bind() leaves
// a stale file behind and a mode of 0777 minus the umask, sun_path is short,
// and a directory or a socket that someone else made must not be trusted.
//
// **Blocking.** PrepareSocketPath() does file-system work (it can create a
// directory) and belongs on a thread that may block. The others are single
// system calls on a path that is already known to be in a private directory.
namespace koan::agent {

// The longest socket path that fits in sun_path on every platform (macOS: 104
// bytes with the terminator).
inline constexpr size_t kMaxSocketPathLength = 103;

// The per-user temporary directory (macOS: confstr(_CS_DARWIN_USER_TEMP_DIR), a
// 0700 directory only this user can enter), or /tmp where there is none. A
// shared /tmp lets another local user create `koan-<uid>` first, which can only
// deny service but is avoidable.
base::FilePath UserTempRoot();

// <profile>/AgentControl/agent.sock, or if that is too long for sun_path (a
// deep --user-data-dir), <tmp_root>/koan-<uid>/<16 hex of the SHA-256 of the
// profile path>.sock. Without `tmp_root` the per-user temporary directory is
// tried, then /tmp. nullopt if nothing fits.
std::optional<base::FilePath> ChooseSocketPath(
    const base::FilePath& profile_dir,
    uid_t uid,
    std::optional<base::FilePath> tmp_root = std::nullopt);

enum class SocketPrepareResult {
  kOk,
  // The directory is a symlink, not a directory, or someone else's, or the
  // lock file is not a plain file of ours.
  kDirectoryUnsafe,
  // Something that is not a socket of ours is at the path.
  kNotOurs,
  // Another browser holds the lock.
  kInUse,
  kError,
};

// Makes the directory of `socket_path` (0700, owned by `uid`, not a symlink;
// one that is ours but looser is tightened), takes an exclusive lock on
// `<socket file name>.lock` beside the socket (one lock per socket, not per
// directory), and removes a socket at the path.
//
// The lock is what says whether a socket is dead: whoever holds it is the
// browser that owns the path, and the kernel drops it when that browser
// crashes. (Probing with connect() would be wrong: a full backlog is a refusal
// on macOS, and the probe makes a live gateway accept a connection.) Hold
// `*lock` for as long as the socket is in use. Anything but a socket at the
// path is left alone.
SocketPrepareResult PrepareSocketPath(const base::FilePath& socket_path,
                                      uid_t uid,
                                      base::ScopedFD* lock);

// Which file a socket path named when it was bound, so that only that file is
// removed afterwards: by then the path may name a different browser's socket.
struct SocketIdentity {
  dev_t device = 0;
  ino_t inode = 0;

  bool operator==(const SocketIdentity&) const = default;
};

// The identity of the socket at `socket_path` if it is a socket owned by `uid`.
std::optional<SocketIdentity> SocketIdentityOf(
    const base::FilePath& socket_path,
    uid_t uid);

// After bind(): 0600, and checks that it is a socket owned by `uid`.
bool RestrictSocketPermissions(const base::FilePath& socket_path, uid_t uid);

// Removes the file at `socket_path` if it is still the socket `identity`
// names. False (and nothing removed) if it is gone, or is another file.
bool RemoveSocketIfSame(const base::FilePath& socket_path,
                        const SocketIdentity& identity);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_SOCKET_H_
