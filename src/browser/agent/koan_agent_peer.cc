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
#endif

namespace koan::agent {

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

std::unique_ptr<PeerResolver> CreatePlatformPeerResolver() {
  return std::make_unique<MacPeerResolver>();
}

#else

std::unique_ptr<PeerResolver> CreatePlatformPeerResolver() {
  return nullptr;
}

#endif

}  // namespace koan::agent
