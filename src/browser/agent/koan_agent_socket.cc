// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_socket.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/hash.h"
#include "net/base/sockaddr_storage.h"
#include "net/base/sockaddr_util_posix.h"

namespace koan::agent {

namespace {

bool Fits(const base::FilePath& path) {
  return path.value().size() <= kMaxSocketPathLength;
}

// Whether something is listening on the socket at `path`. nullopt if it could
// not be told.
std::optional<bool> IsListening(const base::FilePath& path) {
  net::SockaddrStorage address;
  if (!net::FillUnixAddress(path.value(), /*use_abstract_namespace=*/false,
                            &address)) {
    return std::nullopt;
  }
  base::ScopedFD fd(socket(AF_UNIX, SOCK_STREAM, 0));
  if (!fd.is_valid()) {
    return std::nullopt;
  }
  // A blocking connect to a unix socket returns at once: it succeeds, or is
  // refused, or (a full backlog) fails with EAGAIN, which is "alive".
  if (connect(fd.get(), address.addr(), address.addr_len) == 0) {
    return true;
  }
  switch (errno) {
    case ECONNREFUSED:
    case ENOENT:
      return false;
    case EAGAIN:
    case EPROTOTYPE:
      return true;
    default:
      return std::nullopt;
  }
}

}  // namespace

std::optional<base::FilePath> ChooseSocketPath(
    const base::FilePath& profile_dir,
    uid_t uid,
    const base::FilePath& tmp_root) {
  const base::FilePath preferred =
      profile_dir.Append("AgentControl").Append("agent.sock");
  if (Fits(preferred)) {
    return preferred;
  }
  const std::string digest =
      base::HexEncodeLower(crypto::hash::Sha256(profile_dir.value()));
  const base::FilePath fallback =
      tmp_root.Append("koan-" + base::NumberToString(uid))
          .Append(digest.substr(0, 16) + ".sock");
  if (Fits(fallback)) {
    return fallback;
  }
  return std::nullopt;
}

SocketPrepareResult PrepareSocketPath(const base::FilePath& socket_path,
                                      uid_t uid) {
  if (!Fits(socket_path)) {
    return SocketPrepareResult::kError;
  }
  const base::FilePath directory = socket_path.DirName();

  struct stat info;
  if (lstat(directory.value().c_str(), &info) != 0) {
    if (errno != ENOENT) {
      return SocketPrepareResult::kError;
    }
    if (!base::CreateDirectory(directory)) {
      return SocketPrepareResult::kError;
    }
    if (chmod(directory.value().c_str(), 0700) != 0) {
      return SocketPrepareResult::kError;
    }
    if (lstat(directory.value().c_str(), &info) != 0) {
      return SocketPrepareResult::kError;
    }
  }
  // lstat, so a symlink is not followed and is not a directory here.
  if (!S_ISDIR(info.st_mode) || info.st_uid != uid) {
    return SocketPrepareResult::kDirectoryUnsafe;
  }
  if ((info.st_mode & 077) != 0 &&
      chmod(directory.value().c_str(), 0700) != 0) {
    return SocketPrepareResult::kDirectoryUnsafe;
  }

  if (lstat(socket_path.value().c_str(), &info) != 0) {
    return errno == ENOENT ? SocketPrepareResult::kOk
                           : SocketPrepareResult::kError;
  }
  if (!S_ISSOCK(info.st_mode) || info.st_uid != uid) {
    return SocketPrepareResult::kNotOurs;
  }
  const std::optional<bool> listening = IsListening(socket_path);
  if (!listening) {
    return SocketPrepareResult::kError;
  }
  if (*listening) {
    return SocketPrepareResult::kInUse;
  }
  if (unlink(socket_path.value().c_str()) != 0 && errno != ENOENT) {
    return SocketPrepareResult::kError;
  }
  return SocketPrepareResult::kOk;
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

}  // namespace koan::agent
