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
#include <string>
#include <type_traits>

#include "base/apple/scoped_cftyperef.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#endif

namespace koan::agent {

PeerResolver::~PeerResolver() = default;

#if BUILDFLAG(IS_MAC)

namespace {

constexpr int kMaxAncestors = 12;

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
std::optional<std::string> SigningIdentity(SecCodeRef code) {
  const OSStatus validity =
      SecCodeCheckValidity(code, kSecCSDefaultFlags, nullptr);
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
    Handle handle = {};
    if (bytes.size() != sizeof(handle.token)) {
      return std::nullopt;
    }
    base::byte_span_from_ref(handle.token).copy_from(bytes);
    const pid_t pid = audit_token_to_pid(handle.token);

    proc_uniqidentifierinfo before;
    if (pid <= 0 || !UniqueInfo(pid, &before)) {
      return std::nullopt;
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
      return std::nullopt;
    }
    std::optional<std::string> signing = SigningIdentity(code.get());
    const std::string path = ExecutablePath(pid);
    proc_uniqidentifierinfo after;
    if (!signing || path.empty() || !UniqueInfo(pid, &after) ||
        after.p_uniqueid != before.p_uniqueid ||
        uuid_compare(after.p_uuid, before.p_uuid) != 0) {
      return std::nullopt;
    }

    handle.unique_id = after.p_uniqueid;
    uuid_copy(handle.executable_uuid, after.p_uuid);
    ResolvedPeer peer;
    peer.identity.uid = audit_token_to_euid(handle.token);
    peer.identity.process = ProcessInfo{path, *signing};
    peer.identity.ancestors = Ancestors(pid);
    const auto handle_bytes = base::byte_span_from_ref(handle);
    peer.handle.assign(handle_bytes.begin(), handle_bytes.end());
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
  // never silently gets shorter.
  static std::vector<ProcessInfo> Ancestors(pid_t pid) {
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
      ancestor.signing_identity =
          SigningIdentityOfPid(parent).value_or("unverified");
      chain.push_back(std::move(ancestor));
      current = parent;
    }
    return chain;
  }
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
