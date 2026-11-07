// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_peer.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <bsm/libbsm.h>
#include <libproc.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <uuid/uuid.h>

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

#if BUILDFLAG(IS_MAC)

namespace {

constexpr int kMaxAncestors = 12;
// Entries kept of what an ancestor's signature said, per (path, process).
constexpr size_t kMaxCachedAncestors = 256;
// A lookup that takes this long is logged, so that a slow one can be told from
// one that failed (the gateway gives up on a connection after 5 s).
constexpr base::TimeDelta kSlowLookup = base::Seconds(1);

// What Unchanged() compares: the audit token the peer was found by, and what
// the kernel says about that process incarnation. p_uniqueid is per fork; the
// executable's uuid changes with an exec.
struct Handle {
  audit_token_t token;
  uint64_t unique_id;
  uuid_t executable_uuid;
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

bool UniqueInfo(pid_t pid, proc_uniqidentifierinfo* info) {
  return proc_pidinfo(pid, PROC_PIDUNIQIDENTIFIERINFO, 0, info,
                      sizeof(*info)) == static_cast<int>(sizeof(*info));
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
base::apple::ScopedCFTypeRef<SecCodeRef> GuestCode(CFDictionaryRef attributes) {
  base::apple::ScopedCFTypeRef<SecCodeRef> code;
  if (SecCodeCopyGuestWithAttributes(nullptr, attributes, kSecCSDefaultFlags,
                                     code.InitializeInto()) != errSecSuccess) {
    return base::apple::ScopedCFTypeRef<SecCodeRef>();
  }
  return code;
}

// "apple" for a platform binary, "team:<id>" for a developer-signed one,
// "cdhash:<hex>" for one that is signed without a team (ad hoc), "" for one
// that is not signed at all. nullopt if it is signed and does not verify: a
// process whose signature is broken is not one to name.
//
// The check is kSecCSBasicValidateOnly for the peer and for every ancestor: the
// code directory and the signature (and with them the team id and the cdhash,
// which are what the identity is made of), not every resource of the bundle.
// The default would hash all of a large app (an editor, a browser, an agent
// shipped as an Electron app) and take many seconds, and the gateway gives up
// on a connection after 5. The running process is held to what the kernel
// enforces (its code signing status), which the guest lookup by audit token has
// already established; a resource swapped on disk after launch is not the
// question here. kSecCSCheckAllArchitectures and a designated-requirement
// evaluation are left out for the same reason.
std::optional<std::string> SigningIdentity(SecCodeRef code) {
  const OSStatus validity =
      SecCodeCheckValidity(code, kSecCSBasicValidateOnly, nullptr);
  if (validity == errSecCSUnsigned) {
    return std::string();
  }
  if (validity != errSecSuccess) {
    return std::nullopt;
  }
  base::apple::ScopedCFTypeRef<SecStaticCodeRef> static_code;
  if (SecCodeCopyStaticCode(code, kSecCSDefaultFlags,
                            static_code.InitializeInto()) != errSecSuccess) {
    return std::nullopt;
  }
  base::apple::ScopedCFTypeRef<CFDictionaryRef> info;
  if (SecCodeCopySigningInformation(static_code.get(), kSecCSSigningInformation,
                                    info.InitializeInto()) != errSecSuccess) {
    return std::nullopt;
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
  auto code = GuestCode(attributes.get());
  if (!code) {
    return std::nullopt;
  }
  return SigningIdentity(code.get());
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
    const auto refuse = [&](const char* reason) -> std::optional<ResolvedPeer> {
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

    proc_uniqidentifierinfo before;
    if (pid <= 0 || !UniqueInfo(pid, &before)) {
      return refuse("the process has exited");
    }
    // The audit token names one incarnation of the pid: if the process has
    // exec'd or the pid was reused since the connect, this finds nothing.
    base::apple::ScopedCFTypeRef<CFDataRef> token_data(CFDataCreate(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(&handle.token),
        sizeof(handle.token)));
    auto attributes =
        AttributeDictionary(kSecGuestAttributeAudit, token_data.get());
    auto code = GuestCode(attributes.get());
    if (!code) {
      return refuse("no process for the audit token");
    }
    std::optional<std::string> signing = SigningIdentity(code.get());
    if (!signing) {
      return refuse("the signature does not verify");
    }
    const std::string path = ExecutablePath(pid);
    proc_uniqidentifierinfo after;
    if (path.empty()) {
      return refuse("no executable path");
    }
    if (!UniqueInfo(pid, &after) || after.p_uniqueid != before.p_uniqueid ||
        uuid_compare(after.p_uuid, before.p_uuid) != 0) {
      return refuse("the process changed while it was looked at");
    }

    handle.unique_id = after.p_uniqueid;
    uuid_copy(handle.executable_uuid, after.p_uuid);
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
    proc_uniqidentifierinfo now;
    return pid > 0 && UniqueInfo(pid, &now) &&
           now.p_uniqueid == handle.unique_id &&
           uuid_compare(now.p_uuid, handle.executable_uuid) == 0 &&
           ExecutablePath(pid) == peer.identity.process.path;
  }

 private:
  // The parents, nearest first, without launchd. A parent that exits while it
  // is looked at is recorded as unverified rather than dropped, so a chain
  // never silently gets shorter. What an ancestor's signature said is kept for
  // the life of that process (a shell or an editor is the parent of many
  // connections), keyed by its path and the kernel's unique id.
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
    proc_uniqidentifierinfo info;
    const bool keyed = !path.empty() && UniqueInfo(pid, &info);
    const std::pair<std::string, uint64_t> key(path,
                                               keyed ? info.p_uniqueid : 0);
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
