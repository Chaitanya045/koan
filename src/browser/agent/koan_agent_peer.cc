// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_peer.h"

#include <sys/socket.h>

#include "base/posix/eintr_wrapper.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <bsm/libbsm.h>
#include <libproc.h>
#include <sys/un.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <string>
#include <type_traits>
#include <utility>

#include "base/apple/scoped_cftyperef.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/timer/elapsed_timer.h"
#elif BUILDFLAG(IS_LINUX)
#include <fcntl.h>
#include <limits.h>
#include <linux/magic.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

#include <array>
#include <map>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/posix/unix_domain_socket.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/timer/elapsed_timer.h"
#endif

namespace koan::agent {

PeerDescriptors::~PeerDescriptors() = default;

ResolvedPeer::ResolvedPeer() = default;
ResolvedPeer::ResolvedPeer(const ResolvedPeer&) = default;
ResolvedPeer& ResolvedPeer::operator=(const ResolvedPeer&) = default;
ResolvedPeer::ResolvedPeer(ResolvedPeer&&) = default;
ResolvedPeer& ResolvedPeer::operator=(ResolvedPeer&&) = default;
ResolvedPeer::~ResolvedPeer() = default;

PeerResolver::~PeerResolver() = default;

bool PeerHasHungUp(int fd) {
  char byte;
  // 0 is the end of the stream with nothing before it; data, an error or
  // "nothing yet" (EAGAIN) is not.
  return HANDLE_EINTR(recv(fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT)) == 0;
}

#if BUILDFLAG(IS_MAC)

namespace {

constexpr int kMaxAncestors = 12;
// Entries kept of what an ancestor's signature said, per (path, process).
constexpr size_t kMaxCachedAncestors = 256;
// A lookup that takes this long is logged, so that a slow one can be told from
// one that failed (the gateway gives up on a connection after 5 s).
constexpr base::TimeDelta kSlowLookup = base::Seconds(1);

// When a process started, as the kernel records it (public API: proc_bsdinfo).
// A pid that was reused is a process that started later, so (pid, start time)
// names one process for as long as it lives. (The audit token's pid version
// would say more, in particular that the process has exec'd, but there is no
// public call that reads the current one; the executable's path stands in for
// that, below.)
struct StartTime {
  uint64_t seconds;
  uint64_t microseconds;
};

// What Unchanged() compares: the audit token the peer was found by, and when
// that process started.
struct Handle {
  audit_token_t token;
  StartTime started;
};
static_assert(std::is_trivially_copyable_v<Handle>);
static_assert(std::has_unique_object_representations_v<Handle>);

std::string ExecutablePath(pid_t pid) {
  std::array<char, PROC_PIDPATHINFO_MAXSIZE> path;
  const int length = proc_pidpath(pid, path.data(), path.size());
  if (length <= 0 || static_cast<size_t>(length) >= path.size()) {
    return std::string();
  }
  return std::string(path.begin(), std::next(path.begin(), length));
}

bool ProcessStart(pid_t pid, StartTime* started) {
  proc_bsdinfo info;
  if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) !=
      static_cast<int>(sizeof(info))) {
    return false;
  }
  started->seconds = info.pbi_start_tvsec;
  started->microseconds = info.pbi_start_tvusec;
  return true;
}

bool SameStart(const StartTime& a, const StartTime& b) {
  return a.seconds == b.seconds && a.microseconds == b.microseconds;
}

base::apple::ScopedCFTypeRef<CFDictionaryRef> AttributeDictionary(
    CFStringRef key,
    CFTypeRef value) {
  const void* keys[] = {key};
  const void* values[] = {value};
  return base::apple::ScopedCFTypeRef<CFDictionaryRef>(CFDictionaryCreate(
      kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks));
}

// The running process the attributes name, or null.
base::apple::ScopedCFTypeRef<SecCodeRef> GuestCode(CFDictionaryRef attributes,
                                                   OSStatus* status) {
  base::apple::ScopedCFTypeRef<SecCodeRef> code;
  *status = SecCodeCopyGuestWithAttributes(
      nullptr, attributes, kSecCSDefaultFlags, code.InitializeInto());
  if (*status != errSecSuccess) {
    return base::apple::ScopedCFTypeRef<SecCodeRef>();
  }
  return code;
}

// "apple" for a platform binary, "team:<id>" for a developer-signed one,
// "cdhash:<hex>" for one that is signed without a team (ad hoc), "" for one
// that is not signed at all. nullopt if it is signed and does not verify: a
// process whose signature is broken is not one to name.
//
// This is the *dynamic* check (SecCodeCheckValidity on the running process,
// with no flags: kSecCSBasicValidateOnly belongs to the static check, and with
// it the first Mac run refused every peer): the code directory and the kernel's
// code signing status, not every resource of a bundle (that is
// SecStaticCodeCheckValidity, which is never called, because it would hash all
// of a large app and take many seconds where the gateway gives up after 5). The
// team id and the cdhash that the identity is made of come from the code
// directory. A designated-requirement evaluation is left out for the same
// reason. On failure `*why` says which call failed with which OSStatus.
std::optional<std::string> SigningIdentity(SecCodeRef code, std::string* why) {
  const auto fail = [&](const char* call, OSStatus status) {
    if (why) {
      *why = std::string(call) + " " + base::NumberToString(status);
    }
    return std::optional<std::string>();
  };
  base::apple::ScopedCFTypeRef<CFErrorRef> error;
  const OSStatus validity = SecCodeCheckValidityWithErrors(
      code, kSecCSDefaultFlags, nullptr, error.InitializeInto());
  if (validity == errSecCSUnsigned) {
    return std::string();
  }
  if (validity != errSecSuccess) {
    std::string call = "SecCodeCheckValidityWithErrors";
    if (error) {
      base::apple::ScopedCFTypeRef<CFStringRef> text(
          CFErrorCopyDescription(error.get()));
      if (text) {
        call += " (" + base::SysCFStringRefToUTF8(text.get()) + ")";
      }
    }
    return fail(call.c_str(), validity);
  }
  base::apple::ScopedCFTypeRef<SecStaticCodeRef> static_code;
  OSStatus status = SecCodeCopyStaticCode(code, kSecCSDefaultFlags,
                                          static_code.InitializeInto());
  if (status != errSecSuccess) {
    return fail("SecCodeCopyStaticCode", status);
  }
  base::apple::ScopedCFTypeRef<CFDictionaryRef> info;
  status = SecCodeCopySigningInformation(
      static_code.get(), kSecCSSigningInformation, info.InitializeInto());
  if (status != errSecSuccess) {
    return fail("SecCodeCopySigningInformation", status);
  }
  const void* platform =
      CFDictionaryGetValue(info.get(), kSecCodeInfoPlatformIdentifier);
  int32_t platform_value = 0;
  if (platform && CFGetTypeID(platform) == CFNumberGetTypeID() &&
      CFNumberGetValue(static_cast<CFNumberRef>(platform), kCFNumberSInt32Type,
                       &platform_value) &&
      platform_value != 0) {
    return std::string("apple");
  }
  const void* team =
      CFDictionaryGetValue(info.get(), kSecCodeInfoTeamIdentifier);
  if (team && CFGetTypeID(team) == CFStringGetTypeID()) {
    return "team:" + base::SysCFStringRefToUTF8(static_cast<CFStringRef>(team));
  }
  const void* unique = CFDictionaryGetValue(info.get(), kSecCodeInfoUnique);
  if (unique && CFGetTypeID(unique) == CFDataGetTypeID()) {
    const CFDataRef hash = static_cast<CFDataRef>(unique);
    return "cdhash:" + base::HexEncodeLower(UNSAFE_BUFFERS(base::span(
                           CFDataGetBytePtr(hash),
                           static_cast<size_t>(CFDataGetLength(hash)))));
  }
  return std::string();
}

std::optional<std::string> SigningIdentityOfPid(pid_t pid) {
  const int32_t value = pid;
  base::apple::ScopedCFTypeRef<CFNumberRef> number(
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value));
  auto attributes = AttributeDictionary(kSecGuestAttributePid, number.get());
  OSStatus status = errSecSuccess;
  auto code = GuestCode(attributes.get(), &status);
  if (!code) {
    VLOG(1) << "agent peer: no code for ancestor " << pid << ": " << status;
    return std::nullopt;
  }
  std::string why;
  std::optional<std::string> identity = SigningIdentity(code.get(), &why);
  if (!identity) {
    VLOG(1) << "agent peer: ancestor " << pid << ": " << why;
  }
  return identity;
}

class MacPeerResolver final : public PeerResolver {
 public:
  std::optional<PeerToken> TokenForSocket(int fd) const override {
    audit_token_t token;
    socklen_t length = sizeof(token);
    if (getsockopt(fd, SOL_LOCAL, LOCAL_PEERTOKEN, &token, &length) != 0 ||
        length != sizeof(token)) {
      return std::nullopt;
    }
    const auto bytes = base::byte_span_from_ref(token);
    return PeerToken(bytes.begin(), bytes.end());
  }

  std::optional<ResolvedPeer> Resolve(const PeerToken& bytes) const override {
    base::ElapsedTimer timer;
    // Every way out says why: a connection that is dropped here is dropped
    // without a word to the client, so the log is where it can be found.
    const auto refuse =
        [&](const std::string& reason) -> std::optional<ResolvedPeer> {
      LOG(WARNING) << "agent peer: not resolved (" << reason << ") after "
                   << timer.Elapsed().InMilliseconds() << " ms";
      return std::nullopt;
    };
    Handle handle = {};
    if (bytes.size() != sizeof(handle.token)) {
      return refuse("not an audit token");
    }
    base::byte_span_from_ref(handle.token).copy_from(bytes);
    const pid_t pid = audit_token_to_pid(handle.token);

    StartTime before;
    if (pid <= 0 || !ProcessStart(pid, &before)) {
      return refuse("the process has exited");
    }
    // The audit token names one incarnation of the pid: if the process has
    // exec'd or the pid was reused since the connect, this finds nothing.
    base::apple::ScopedCFTypeRef<CFDataRef> token_data(CFDataCreate(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(&handle.token),
        sizeof(handle.token)));
    auto attributes =
        AttributeDictionary(kSecGuestAttributeAudit, token_data.get());
    OSStatus guest_status = errSecSuccess;
    auto code = GuestCode(attributes.get(), &guest_status);
    if (!code) {
      return refuse(
          "no process for the audit token: "
          "SecCodeCopyGuestWithAttributes " +
          base::NumberToString(guest_status));
    }
    std::string why;
    std::optional<std::string> signing = SigningIdentity(code.get(), &why);
    if (!signing) {
      return refuse("the signature does not verify: " + why);
    }
    const std::string path = ExecutablePath(pid);
    StartTime after;
    if (path.empty()) {
      return refuse("no executable path");
    }
    if (!ProcessStart(pid, &after) || !SameStart(after, before)) {
      return refuse("the process changed while it was looked at");
    }

    handle.started = after;
    ResolvedPeer peer;
    peer.identity.uid = audit_token_to_euid(handle.token);
    peer.identity.process = ProcessInfo{path, *signing};
    peer.identity.ancestors = Ancestors(pid);
    const auto handle_bytes = base::byte_span_from_ref(handle);
    peer.handle.assign(handle_bytes.begin(), handle_bytes.end());
    if (timer.Elapsed() > kSlowLookup) {
      LOG(WARNING) << "agent peer: slow lookup of " << path << ": "
                   << timer.Elapsed().InMilliseconds() << " ms, "
                   << peer.identity.ancestors.size() << " ancestors";
    }
    return peer;
  }

  bool Unchanged(const ResolvedPeer& peer) const override {
    Handle handle;
    if (peer.handle.size() != sizeof(handle)) {
      return false;
    }
    base::byte_span_from_ref(handle).copy_from(peer.handle);
    const pid_t pid = audit_token_to_pid(handle.token);
    // The same process (it started when it did) still running the same
    // executable (an exec of another program changes the path).
    StartTime now;
    return pid > 0 && ProcessStart(pid, &now) &&
           SameStart(now, handle.started) &&
           ExecutablePath(pid) == peer.identity.process.path;
  }

 private:
  // The parents, nearest first, without launchd. A parent that exits while it
  // is looked at is recorded as unverified rather than dropped, so a chain
  // never silently gets shorter. What an ancestor's signature said is kept for
  // the life of that process (a shell or an editor is the parent of many
  // connections), keyed by its path and when it started.
  std::vector<ProcessInfo> Ancestors(pid_t pid) const {
    std::vector<ProcessInfo> chain;
    pid_t current = pid;
    for (int depth = 0; depth < kMaxAncestors; ++depth) {
      proc_bsdinfo info;
      if (proc_pidinfo(current, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) !=
          static_cast<int>(sizeof(info))) {
        break;
      }
      const pid_t parent = static_cast<pid_t>(info.pbi_ppid);
      if (parent <= 1 || parent == current) {
        break;
      }
      ProcessInfo ancestor;
      ancestor.path = ExecutablePath(parent);
      ancestor.signing_identity = AncestorIdentity(parent, ancestor.path);
      chain.push_back(std::move(ancestor));
      current = parent;
    }
    return chain;
  }

  std::string AncestorIdentity(pid_t pid, const std::string& path) const {
    StartTime started = {};
    const bool keyed = !path.empty() && ProcessStart(pid, &started);
    const std::pair<std::string, uint64_t> key(
        path, keyed ? started.seconds * 1000000 + started.microseconds : 0);
    if (keyed) {
      base::AutoLock lock(cache_lock_);
      const auto it = ancestor_cache_.find(key);
      if (it != ancestor_cache_.end()) {
        return it->second;
      }
    }
    std::optional<std::string> identity = SigningIdentityOfPid(pid);
    if (!identity) {
      return "unverified";
    }
    if (keyed) {
      base::AutoLock lock(cache_lock_);
      if (ancestor_cache_.size() >= kMaxCachedAncestors) {
        ancestor_cache_.clear();
      }
      ancestor_cache_[key] = *identity;
    }
    return *identity;
  }

  mutable base::Lock cache_lock_;
  mutable std::map<std::pair<std::string, uint64_t>, std::string>
      ancestor_cache_;
};

}  // namespace

std::unique_ptr<PeerResolver> CreatePlatformPeerResolver(PeerHello) {
  return std::make_unique<MacPeerResolver>();
}

bool SendPeerHello(int) {
  return true;
}

#elif BUILDFLAG(IS_LINUX)

namespace {

// Linux 6.5 (asm-generic/socket.h); Chromium's sysroot predates it.
#ifndef SO_PEERPIDFD
#define SO_PEERPIDFD 77
#endif

constexpr int kMaxAncestors = 12;
// The procfs root's inode (PROC_ROOT_INO in the kernel).
constexpr ino_t kProcRootInode = 1;
// How long a gateway's worker waits for the hello after the connection: a
// client sends it right after connect(), before anything else.
constexpr int kHelloWaitMs = 3000;
// Tokens handed out and not yet resolved. The gateway admits 8 connections; a
// token over this many pushes out the oldest, whose Resolve() then fails.
constexpr size_t kMaxPendingTokens = 64;
// The largest /proc file read here (stat, fdinfo).
constexpr size_t kMaxProcFile = 16 * 1024;
constexpr base::TimeDelta kSlowLookup = base::Seconds(1);

// The contents of a small file under `dir` (a /proc directory), or nullopt.
std::optional<std::string> ReadAt(int dir, const std::string& name) {
  base::ScopedFD fd(
      HANDLE_EINTR(openat(dir, name.c_str(), O_RDONLY | O_CLOEXEC)));
  if (!fd.is_valid()) {
    return std::nullopt;
  }
  std::string text;
  std::array<char, 4096> buffer;
  while (text.size() < kMaxProcFile) {
    const ssize_t length =
        HANDLE_EINTR(read(fd.get(), buffer.data(), buffer.size()));
    if (length < 0) {
      return std::nullopt;
    }
    if (length == 0) {
      break;
    }
    text.append(base::as_string_view(
        base::span(buffer).first(static_cast<size_t>(length))));
  }
  return text;
}

// The executable of the process whose /proc directory is `dir`, as the kernel
// names it (a path in the program's own mount namespace), or "".
std::string ExecutablePath(int dir) {
  std::array<char, PATH_MAX> path;
  const ssize_t length = readlinkat(dir, "exe", path.data(), path.size());
  if (length <= 0 || static_cast<size_t>(length) >= path.size()) {
    return std::string();
  }
  return std::string(base::as_string_view(
      base::span(path).first(static_cast<size_t>(length))));
}

// The process's start, so that a pid that was reused is told apart.
std::optional<ProcStat> StatOf(int dir) {
  std::optional<std::string> text = ReadAt(dir, "stat");
  return text ? ParseProcStat(*text) : std::nullopt;
}

// The identity of the executable of the process whose /proc directory is
// `dir`: FileIdentity() of the file `exe` names (opened through the link, so
// the file itself, whatever path it has now). On a FUSE file system the inode
// and times are whatever the file system's server says, so the identity is
// this process alone ("process:<pid>:<start>"): every new process asks again,
// and none can borrow another's pin. nullopt if there is no file to look at.
std::optional<std::string> ExecutableIdentity(int dir,
                                              int pid,
                                              uint64_t start_time) {
  base::ScopedFD exe(HANDLE_EINTR(openat(dir, "exe", O_PATH | O_CLOEXEC)));
  struct stat info;
  struct statfs file_system;
  if (!exe.is_valid() || fstat(exe.get(), &info) != 0 ||
      fstatfs(exe.get(), &file_system) != 0 || !S_ISREG(info.st_mode)) {
    return std::nullopt;
  }
  if (file_system.f_type == FUSE_SUPER_MAGIC) {
    return "process:" + base::NumberToString(pid) + ":" +
           base::NumberToString(start_time);
  }
  return FileIdentity(info.st_ino, info.st_ctim.tv_sec, info.st_ctim.tv_nsec,
                      info.st_size);
}

// Whether `fd` is the root of a procfs.
bool IsProcRoot(int fd) {
  struct statfs file_system;
  struct stat info;
  return fstatfs(fd, &file_system) == 0 &&
         file_system.f_type == PROC_SUPER_MAGIC && fstat(fd, &info) == 0 &&
         S_ISDIR(info.st_mode) && info.st_ino == kProcRootInode;
}

// Whether the process of a pidfd has exited (its pidfd is readable then). A
// pidfd that cannot be polled counts as exited.
bool HasExited(int pidfd) {
  pollfd wait = {pidfd, POLLIN, 0};
  const int ready = HANDLE_EINTR(poll(&wait, 1, 0));
  return ready != 0;
}

// The pid of `pidfd`'s process in the namespace of the procfs `proc`, read
// from this process's own fdinfo of the pidfd, through `proc`. `self` in a
// procfs names the reader only where the reader is visible, so a procfs of a
// namespace this process is not in (one the client made) has no `self`, and
// gives nothing.
std::optional<int> PidThrough(int proc, int pidfd) {
  std::optional<std::string> info =
      ReadAt(proc, "self/fdinfo/" + base::NumberToString(pidfd));
  std::optional<int> pid = info ? PidFromPidfdInfo(*info) : std::nullopt;
  if (!pid || *pid <= 0) {
    return std::nullopt;
  }
  return pid;
}

enum class Hello {
  // The client sent something else first (an older client), or nothing.
  kNone,
  kReceived,
  // The client has gone.
  kGone,
  // A hello byte with no descriptor, or with more than one.
  kMalformed,
};

// Takes the hello from `socket` if it is there, waiting up to kHelloWaitMs for
// the first byte. Takes nothing else: a first byte that is not the hello's is
// left for the session.
Hello ReadHello(int socket, base::ScopedFD* proc) {
  {
    base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
    pollfd wait = {socket, POLLIN, 0};
    if (HANDLE_EINTR(poll(&wait, 1, kHelloWaitMs)) <= 0) {
      return Hello::kNone;
    }
  }
  uint8_t first = 0;
  const ssize_t peeked =
      HANDLE_EINTR(recv(socket, &first, 1, MSG_PEEK | MSG_DONTWAIT));
  if (peeked == 0) {
    return Hello::kGone;
  }
  if (peeked < 0) {
    return errno == EAGAIN || errno == EWOULDBLOCK ? Hello::kNone
                                                   : Hello::kGone;
  }
  if (first != kPeerHelloByte) {
    return Hello::kNone;
  }
  uint8_t byte = 0;
  std::vector<base::ScopedFD> fds;
  if (base::UnixDomainSocket::RecvMsg(socket, base::span_from_ref(byte),
                                      &fds) != 1 ||
      byte != kPeerHelloByte || fds.size() != 1) {
    return Hello::kMalformed;
  }
  *proc = std::move(fds[0]);
  return Hello::kReceived;
}

class LinuxPeerDescriptors final : public PeerDescriptors {
 public:
  LinuxPeerDescriptors(base::ScopedFD pidfd, base::ScopedFD dir)
      : pidfd_(std::move(pidfd)), dir_(std::move(dir)) {}

  int pidfd() const { return pidfd_.get(); }
  int dir() const { return dir_.get(); }

 private:
  base::ScopedFD pidfd_;
  // /proc/<pid> of the process: bound to that process, so reads through it
  // fail once it is gone, even if its pid is reused.
  base::ScopedFD dir_;
};

class LinuxPeerResolver final : public PeerResolver {
 public:
  explicit LinuxPeerResolver(PeerHello hello) : hello_(hello) {}

  std::optional<PeerToken> TokenForSocket(int fd) const override {
    ucred credentials;
    socklen_t length = sizeof(credentials);
    if (fd < 0 ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0 ||
        length != sizeof(credentials)) {
      return std::nullopt;
    }
    Pending pending;
    pending.uid = credentials.uid;
    pending.pid = credentials.pid;
    int pidfd = -1;
    length = sizeof(pidfd);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERPIDFD, &pidfd, &length) == 0 &&
        length == sizeof(pidfd)) {
      pending.pidfd.reset(pidfd);
    }
    if (hello_ == PeerHello::kExpected) {
      // `koan status` only checks that Koan answers: there is nobody to look
      // for (the gateway's quiet path).
      if (PeerHasHungUp(fd)) {
        return std::nullopt;
      }
      // The worker reads the hello from its own descriptor of the socket:
      // the gateway's may be closed and its number reused meanwhile.
      pending.socket.reset(HANDLE_EINTR(fcntl(fd, F_DUPFD_CLOEXEC, 0)));
      if (!pending.socket.is_valid()) {
        return std::nullopt;
      }
    }
    base::AutoLock lock(lock_);
    if (pending_.size() >= kMaxPendingTokens) {
      pending_.erase(pending_.begin());
    }
    const uint64_t id = ++next_id_;
    pending_.emplace(id, std::move(pending));
    const auto bytes = base::byte_span_from_ref(id);
    return PeerToken(bytes.begin(), bytes.end());
  }

  std::optional<ResolvedPeer> Resolve(const PeerToken& token) const override {
    base::ElapsedTimer timer;
    // Every way out says why: a connection that is dropped here is dropped
    // without a word to the client, so the log is where it can be found.
    const auto refuse =
        [&](std::string_view reason) -> std::optional<ResolvedPeer> {
      LOG(WARNING) << "agent peer: not resolved (" << reason << ") after "
                   << timer.Elapsed().InMilliseconds() << " ms";
      return std::nullopt;
    };
    std::optional<Pending> pending = Take(token);
    if (!pending) {
      return refuse("not a token of this resolver, or resolved already");
    }
    base::ScopedFD hello_proc;
    if (pending->socket.is_valid()) {
      switch (ReadHello(pending->socket.get(), &hello_proc)) {
        case Hello::kNone:
        case Hello::kReceived:
          break;
        case Hello::kGone:
          VLOG(1) << "agent peer: the program left before its hello";
          return std::nullopt;
        case Hello::kMalformed:
          return refuse("a malformed hello");
      }
      pending->socket.reset();
    }
    if (!pending->pidfd.is_valid()) {
      return refuse(
          "no pidfd for the connecting program (Linux 6.5 or later is "
          "needed), or it has exited");
    }
    // The /proc to look in: the one the client sent, once the kernel has
    // said it is a procfs in which this process and the peer are both
    // visible; else this process's own.
    base::ScopedFD proc;
    if (hello_proc.is_valid()) {
      if (!IsProcRoot(hello_proc.get())) {
        return refuse("what the hello sent is not a /proc");
      }
      proc = std::move(hello_proc);
    } else {
      proc.reset(HANDLE_EINTR(open("/proc", O_PATH | O_DIRECTORY | O_CLOEXEC)));
      if (!proc.is_valid() || !IsProcRoot(proc.get())) {
        return refuse("no /proc");
      }
    }
    const std::optional<int> pid = PidThrough(proc.get(), pending->pidfd.get());
    if (!pid) {
      if (HasExited(pending->pidfd.get())) {
        VLOG(1) << "agent peer: the program has exited";
        return std::nullopt;
      }
      return refuse(pending->pid == 0
                        ? "the program is outside Koan's sandbox and sent no "
                          "/proc in which it and Koan are both visible (koan "
                          "must run on the host, not in another sandbox)"
                        : "the program is not in /proc");
    }
    base::ScopedFD dir(
        HANDLE_EINTR(openat(proc.get(), base::NumberToString(*pid).c_str(),
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC)));
    // A pid is not reused while its process lives: if the pidfd's process is
    // still there after the directory was opened, the directory is its.
    if (!dir.is_valid() || HasExited(pending->pidfd.get())) {
      VLOG(1) << "agent peer: the program has exited";
      return std::nullopt;
    }
    const std::optional<ProcStat> stat = StatOf(dir.get());
    const std::string path = ExecutablePath(dir.get());
    if (!stat || path.empty()) {
      return refuse("no executable path");
    }
    std::optional<std::string> identity =
        ExecutableIdentity(dir.get(), *pid, stat->start_time);
    if (!identity) {
      return refuse("the executable could not be looked at");
    }

    ResolvedPeer peer;
    peer.identity.uid = pending->uid;
    peer.identity.process = ProcessInfo{path, std::move(*identity)};
    peer.identity.ancestors =
        Ancestors(proc.get(), stat->parent, stat->start_time);
    const std::array<uint64_t, 2> handle = {static_cast<uint64_t>(*pid),
                                            stat->start_time};
    const auto handle_bytes = base::as_byte_span(handle);
    peer.handle.assign(handle_bytes.begin(), handle_bytes.end());
    peer.descriptors = std::make_shared<LinuxPeerDescriptors>(
        std::move(pending->pidfd), std::move(dir));
    if (timer.Elapsed() > kSlowLookup) {
      LOG(WARNING) << "agent peer: slow lookup of " << path << ": "
                   << timer.Elapsed().InMilliseconds() << " ms, "
                   << peer.identity.ancestors.size() << " ancestors";
    }
    return peer;
  }

  bool Unchanged(const ResolvedPeer& peer) const override {
    // Only this resolver makes these.
    const auto* held =
        static_cast<const LinuxPeerDescriptors*>(peer.descriptors.get());
    // The same process (its pidfd has not fired), still running the same
    // executable (an exec of another program changes the link).
    return held && !peer.identity.process.path.empty() &&
           !HasExited(held->pidfd()) &&
           ExecutablePath(held->dir()) == peer.identity.process.path;
  }

 private:
  struct Pending {
    uid_t uid = 0;
    // As SO_PEERCRED saw it, in this process's namespace: 0 for a program
    // outside it (the host, seen from the Flatpak).
    pid_t pid = 0;
    base::ScopedFD pidfd;
    // For the hello (PeerHello::kExpected only).
    base::ScopedFD socket;
  };

  std::optional<Pending> Take(const PeerToken& token) const {
    uint64_t id = 0;
    if (token.size() != sizeof(id)) {
      return std::nullopt;
    }
    base::byte_span_from_ref(id).copy_from(token);
    base::AutoLock lock(lock_);
    auto it = pending_.find(id);
    if (it == pending_.end()) {
      return std::nullopt;
    }
    std::optional<Pending> pending(std::move(it->second));
    pending_.erase(it);
    return pending;
  }

  // The parents, nearest first, without init (pid 1 of the namespace `proc`
  // shows). A parent that cannot be read (it has exited, or is another
  // user's) is recorded as unverified, and one that started after its child
  // is a later process that reused the pid: the chain stops there, so it
  // never names a stranger and never silently gets shorter.
  std::vector<ProcessInfo> Ancestors(int proc,
                                     int parent,
                                     uint64_t child_start) const {
    std::vector<ProcessInfo> chain;
    for (int depth = 0; depth < kMaxAncestors && parent > 1; ++depth) {
      base::ScopedFD dir(
          HANDLE_EINTR(openat(proc, base::NumberToString(parent).c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC)));
      const std::optional<ProcStat> stat =
          dir.is_valid() ? StatOf(dir.get()) : std::nullopt;
      if (!stat || stat->start_time > child_start) {
        chain.push_back(ProcessInfo{std::string(), "unverified"});
        break;
      }
      ProcessInfo ancestor;
      ancestor.path = ExecutablePath(dir.get());
      std::optional<std::string> identity =
          ancestor.path.empty()
              ? std::nullopt
              : ExecutableIdentity(dir.get(), parent, stat->start_time);
      ancestor.signing_identity = identity ? *identity : "unverified";
      chain.push_back(std::move(ancestor));
      child_start = stat->start_time;
      parent = stat->parent;
    }
    return chain;
  }

  const PeerHello hello_;
  mutable base::Lock lock_;
  mutable std::map<uint64_t, Pending> pending_ GUARDED_BY(lock_);
  mutable uint64_t next_id_ GUARDED_BY(lock_) = 0;
};

}  // namespace

std::optional<int> PidFromPidfdInfo(std::string_view fdinfo) {
  for (std::string_view line : base::SplitStringPiece(
           fdinfo, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with("Pid:")) {
      continue;
    }
    int pid = 0;
    if (!base::StringToInt(
            base::TrimWhitespaceASCII(line.substr(4), base::TRIM_ALL), &pid)) {
      return std::nullopt;
    }
    return pid;
  }
  return std::nullopt;
}

std::optional<ProcStat> ParseProcStat(std::string_view stat) {
  // "<pid> (<comm>) <state> <ppid> ...": the name may hold ") ", so the
  // fields start after the last ')'.
  const size_t close = stat.rfind(')');
  if (close == std::string_view::npos) {
    return std::nullopt;
  }
  const std::vector<std::string_view> fields =
      base::SplitStringPiece(stat.substr(close + 1), " ", base::TRIM_WHITESPACE,
                             base::SPLIT_WANT_NONEMPTY);
  // fields[0] is the state (field 3 of proc_pid_stat(5)), fields[1] the
  // parent (4), fields[19] the start time (22).
  ProcStat result;
  if (fields.size() < 20 || !base::StringToInt(fields[1], &result.parent) ||
      !base::StringToUint64(fields[19], &result.start_time) ||
      result.parent < 0) {
    return std::nullopt;
  }
  return result;
}

std::string FileIdentity(ino_t inode,
                         int64_t ctime_seconds,
                         int64_t ctime_nanoseconds,
                         int64_t size) {
  return base::StrCat({"file:", base::NumberToString(inode), ":",
                       base::NumberToString(ctime_seconds), ".",
                       base::NumberToString(ctime_nanoseconds), ":",
                       base::NumberToString(size)});
}

std::unique_ptr<PeerResolver> CreatePlatformPeerResolver(PeerHello hello) {
  return std::make_unique<LinuxPeerResolver>(hello);
}

bool SendPeerHello(int fd) {
  base::ScopedFD proc(
      HANDLE_EINTR(open("/proc", O_PATH | O_DIRECTORY | O_CLOEXEC)));
  if (!proc.is_valid()) {
    return false;
  }
  const uint8_t byte = kPeerHelloByte;
  return base::UnixDomainSocket::SendMsg(fd, base::span_from_ref(byte),
                                         {proc.get()});
}

#else

std::unique_ptr<PeerResolver> CreatePlatformPeerResolver(PeerHello) {
  return nullptr;
}

bool SendPeerHello(int) {
  return true;
}

#endif

}  // namespace koan::agent
