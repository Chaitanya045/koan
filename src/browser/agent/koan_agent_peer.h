// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_PEER_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_PEER_H_

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "build/build_config.h"
#include "koan/browser/agent/koan_agent_auth.h"

// Who is on the other end of an accepted connection, as the OS says it
// (.agent/notes/agent-control.md section 4.1, "Peer check on accept"), never as
// the client says it. Three steps, because they cost differently and run in
// different places:
//
//   TokenForSocket: a getsockopt on the socket, at accept, on the sequence that
//     owns it. On macOS this is LOCAL_PEERTOKEN, the *audit token* of the
//     process that connected: its pid *and* its pid version, which changes when
//     the process execs, so a pid that was reused or exec'd is a different
//     token. (LOCAL_PEERPID is a bare pid and racy.)
//   Resolve: from the token, the executable, its code-signing identity and its
//     ancestors. Slow (the Security framework reads files), so it runs off the
//     sequence, and it has to be callable from any thread. It takes the token
//     and not the descriptor on purpose: the descriptor may be closed, and its
//     number reused by another socket, while this runs.
//   Unchanged: cheap, on the sequence: is that process still the same
//     one (same pid, started at the same time) running the same executable?
//     (The audit token's pid version would say "has it exec'd" directly, but
//     only the Security framework checks it, so it is used once, at Resolve;
//     later the executable's path stands in for it.) The socket survives an
//     exec of its peer and is shared with children that inherit it, so the
//     identity taken at accept is looked at again before a proof is verified
//     and before the user's approval is applied (Session::OnApprovalDecision).
//
// All of them fail closed: nullopt / false means "do not serve this peer".
// What the socket cannot tell: a child that inherited the descriptor is
// indistinguishable from the process that connected, because the token is the
// connector's. That is the connector's decision to share, and it is the
// connector that the user was shown and that the pin names.
//
// **Linux** (design section 4.6, "Linux and the Flatpak"). The token is the
// kernel's pidfd of the process that called connect() (SO_PEERPIDFD, Linux
// 6.5), so a pid that was reused is never mistaken for the peer. Linux has no
// code signatures: the identity of an executable is the file `exe` names
// (FileIdentity()). In the Flatpak the browser is in a PID namespace of its
// own, where a program on the host has no pid and no /proc entry, so a client
// of the gateway sends, with the connection's first byte (the hello,
// SendPeerHello()), a descriptor of its /proc. The resolver uses it only after
// the kernel has confirmed it: a procfs root in which the browser itself is
// visible (a namespace of its own or a parent of it, never one the client made)
// and in which the pidfd's process has a pid. What is read through it is the
// kernel's: the executable, the uid, the parents. Nothing the client says about
// itself is used.
namespace koan::agent {

using PeerToken = std::vector<uint8_t>;

// What a resolver keeps open to recognise a process again (Linux: its pidfd
// and its /proc directory), for as long as anyone holds the ResolvedPeer.
class PeerDescriptors {
 public:
  virtual ~PeerDescriptors();
};

struct ResolvedPeer {
  ResolvedPeer();
  ResolvedPeer(const ResolvedPeer&);
  ResolvedPeer& operator=(const ResolvedPeer&);
  ResolvedPeer(ResolvedPeer&&);
  ResolvedPeer& operator=(ResolvedPeer&&);
  ~ResolvedPeer();

  PeerIdentity identity;
  // Whatever Unchanged() needs to recognise the process again; opaque.
  std::vector<uint8_t> handle;
  // Descriptors that name the process (Linux); null where `handle` is enough
  // (macOS). Shared, because a ResolvedPeer is copied.
  std::shared_ptr<const PeerDescriptors> descriptors;
};

class PeerResolver {
 public:
  virtual ~PeerResolver();

  virtual std::optional<PeerToken> TokenForSocket(int fd) const = 0;
  virtual std::optional<ResolvedPeer> Resolve(const PeerToken& token) const = 0;
  virtual bool Unchanged(const ResolvedPeer& peer) const = 0;
};

// Whether the peers of a resolver send the hello first (Linux; see above).
// Only the gateway's do: the CLI's own resolvers look at programs beside them,
// in their own /proc, and at themselves.
enum class PeerHello {
  kNone,
  kExpected,
};

// The resolver for this platform, or null where there is none (macOS and
// Linux have one, and the gateway does not start without one). `hello` is
// ignored on macOS.
std::unique_ptr<PeerResolver> CreatePlatformPeerResolver(
    PeerHello hello = PeerHello::kNone);

// The first byte a client of the gateway sends on Linux, with a descriptor of
// its /proc. A frame starts with a 4-byte big-endian length of at most
// kMaxPayload, so its first byte is never this one: a client that sends no
// hello is not misread.
inline constexpr uint8_t kPeerHelloByte = 'K';

// Sends the hello on a socket just connected to the gateway (Linux; does
// nothing elsewhere). False if it could not be sent.
bool SendPeerHello(int fd);

// Whether the other end has closed the connection without sending anything,
// as a client that only checks that Koan answers does (`koan status`). Such a
// socket has no peer left for TokenForSocket() to name (on macOS
// LOCAL_PEERTOKEN needs the peer's socket, ENOTCONN once it has gone), so
// there is nobody to look for and nothing to warn about.
bool PeerHasHungUp(int fd);

#if BUILDFLAG(IS_LINUX)
// The pure parts of the Linux resolver, for its tests.

// The pid in a pidfd's /proc/<pid>/fdinfo/<fd> text ("Pid:\t<n>"), in the
// namespace of the procfs it was read through: 0 if the process has no pid
// there, -1 if it has exited; nullopt if there is no such line.
std::optional<int> PidFromPidfdInfo(std::string_view fdinfo);

// From /proc/<pid>/stat: the parent and when the process started (clock ticks
// after boot). The command name in it may hold spaces and parentheses.
struct ProcStat {
  int parent = 0;
  uint64_t start_time = 0;
};
std::optional<ProcStat> ParseProcStat(std::string_view stat);

// The signing_identity of an executable on Linux: "file:<inode>:<ctime
// seconds>.<ctime nanoseconds>:<size>". A new or replaced file is a new inode;
// an edit in place moves the ctime, which only the kernel sets. The device is
// left out: btrfs gives a subvolume another one after a reboot.
std::string FileIdentity(ino_t inode,
                         int64_t ctime_seconds,
                         int64_t ctime_nanoseconds,
                         int64_t size);
#endif

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_PEER_H_
