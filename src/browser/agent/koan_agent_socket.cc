// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_socket.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <string>

#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"
#include "crypto/hash.h"

namespace koan::agent {

namespace {

constexpr char kLockFileName[] = "agent.lock";

bool Fits(const base::FilePath& path) {
  return path.value().size() <= kMaxSocketPathLength;
}

}  // namespace

base::FilePath UserTempRoot() {
#if BUILDFLAG(IS_MAC)
  std::array<char, PATH_MAX> buffer;
  const size_t length =
      confstr(_CS_DARWIN_USER_TEMP_DIR, buffer.data(), buffer.size());
  if (length > 1 && length <= buffer.size()) {
    // confstr counts the terminator, and the directory ends in a slash.
    base::FilePath path(
        std::string(buffer.begin(), buffer.begin() + (length - 1)));
    return path.StripTrailingSeparators();
  }
#endif
  return base::FilePath("/tmp");
}

std::optional<base::FilePath> ChooseSocketPath(
    const base::FilePath& profile_dir,
    uid_t uid,
    std::optional<base::FilePath> tmp_root) {
  const base::FilePath preferred =
      profile_dir.Append("AgentControl").Append("agent.sock");
  if (Fits(preferred)) {
    return preferred;
  }
  const std::string digest =
      base::HexEncodeLower(crypto::hash::Sha256(profile_dir.value()));
  const std::string name = digest.substr(0, 16) + ".sock";
  std::vector<base::FilePath> roots;
  if (tmp_root) {
    roots.push_back(*tmp_root);
  } else {
    roots.push_back(UserTempRoot());
    roots.push_back(base::FilePath("/tmp"));
  }
  for (const base::FilePath& root : roots) {
    const base::FilePath fallback =
        root.Append("koan-" + base::NumberToString(uid)).Append(name);
    if (Fits(fallback)) {
      return fallback;
    }
  }
  return std::nullopt;
}

SocketPrepareResult PrepareSocketPath(const base::FilePath& socket_path,
                                      uid_t uid,
                                      base::ScopedFD* lock) {
  if (!Fits(socket_path)) {
    return SocketPrepareResult::kError;
  }
  const base::FilePath directory = socket_path.DirName();

  // The parents are the profile's or the temporary directory's; the directory
  // itself is ours to make private. mkdir() does not follow a symlink at the
  // end, and everything after it goes through a descriptor opened without
  // following one, so nothing here can be redirected into someone else's
  // directory between a check and a use.
  if (!base::CreateDirectory(directory.DirName())) {
    return SocketPrepareResult::kError;
  }
  if (mkdir(directory.value().c_str(), 0700) != 0 && errno != EEXIST) {
    return SocketPrepareResult::kError;
  }
  base::ScopedFD directory_fd(
      open(directory.value().c_str(),
           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (!directory_fd.is_valid()) {
    // A symlink (ELOOP or ENOTDIR depending on the system) or not a directory.
    return errno == ELOOP || errno == ENOTDIR
               ? SocketPrepareResult::kDirectoryUnsafe
               : SocketPrepareResult::kError;
  }
  struct stat info;
  if (fstat(directory_fd.get(), &info) != 0) {
    return SocketPrepareResult::kError;
  }
  if (!S_ISDIR(info.st_mode) || info.st_uid != uid) {
    return SocketPrepareResult::kDirectoryUnsafe;
  }
  if ((info.st_mode & 077) != 0 && fchmod(directory_fd.get(), 0700) != 0) {
    return SocketPrepareResult::kDirectoryUnsafe;
  }

  // The lock: whoever holds it owns the socket path.
  base::ScopedFD lock_fd(openat(directory_fd.get(), kLockFileName,
                                O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                                0600));
  if (!lock_fd.is_valid()) {
    return errno == ELOOP ? SocketPrepareResult::kDirectoryUnsafe
                          : SocketPrepareResult::kError;
  }
  if (fstat(lock_fd.get(), &info) != 0) {
    return SocketPrepareResult::kError;
  }
  if (!S_ISREG(info.st_mode) || info.st_uid != uid) {
    return SocketPrepareResult::kDirectoryUnsafe;
  }
  if (flock(lock_fd.get(), LOCK_EX | LOCK_NB) != 0) {
    return errno == EWOULDBLOCK ? SocketPrepareResult::kInUse
                                : SocketPrepareResult::kError;
  }

  // With the lock held no browser is listening here: a socket at the path is
  // what a crashed one left, if it is ours.
  if (lstat(socket_path.value().c_str(), &info) == 0) {
    if (!S_ISSOCK(info.st_mode) || info.st_uid != uid) {
      return SocketPrepareResult::kNotOurs;
    }
    if (unlink(socket_path.value().c_str()) != 0 && errno != ENOENT) {
      return SocketPrepareResult::kError;
    }
  } else if (errno != ENOENT) {
    return SocketPrepareResult::kError;
  }
  *lock = std::move(lock_fd);
  return SocketPrepareResult::kOk;
}

std::optional<SocketIdentity> SocketIdentityOf(
    const base::FilePath& socket_path,
    uid_t uid) {
  struct stat info;
  if (lstat(socket_path.value().c_str(), &info) != 0 ||
      !S_ISSOCK(info.st_mode) || info.st_uid != uid) {
    return std::nullopt;
  }
  return SocketIdentity{info.st_dev, info.st_ino};
}

bool RestrictSocketPermissions(const base::FilePath& socket_path, uid_t uid) {
  if (chmod(socket_path.value().c_str(), 0600) != 0) {
    return false;
  }
  struct stat info;
  return lstat(socket_path.value().c_str(), &info) == 0 &&
         S_ISSOCK(info.st_mode) && info.st_uid == uid &&
         (info.st_mode & 0777) == 0600;
}

bool RemoveSocketIfSame(const base::FilePath& socket_path,
                        const SocketIdentity& identity) {
  struct stat info;
  if (lstat(socket_path.value().c_str(), &info) != 0 ||
      !S_ISSOCK(info.st_mode) || info.st_dev != identity.device ||
      info.st_ino != identity.inode) {
    return false;
  }
  return unlink(socket_path.value().c_str()) == 0;
}

}  // namespace koan::agent
