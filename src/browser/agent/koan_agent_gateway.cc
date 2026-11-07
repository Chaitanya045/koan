// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_gateway.h"

#include <unistd.h>

#include <deque>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "koan/browser/agent/koan_agent_socket.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/sockaddr_storage.h"
#include "net/socket/socket_posix.h"
#include "net/socket/unix_domain_server_socket_posix.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace koan::agent {

namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("koan_agent_gateway", R"(
      semantics {
        sender: "Koan agent control"
        description:
          "Replies to a program on the same computer that the user has "
          "allowed to control the browser (the koan command line tool or a "
          "relay), over a unix domain socket only that user can open."
        trigger: "A request from such a program."
        data: "The reply to that request."
        destination: LOCAL
      }
      policy {
        cookies_allowed: NO
        setting:
          "Off by default; turned on and off in the browser's settings."
        policy_exception_justification: "Not implemented."
      })");

constexpr int kListenBacklog = 8;
constexpr int kReadSize = 64 * 1024;

// The kernel already refuses other users' connections to a socket in a 0700
// directory; this closes the accepted socket of any that gets through anyway.
bool AllowUser(uid_t uid, const net::UnixDomainServerSocket::Credentials& who) {
  return who.user_id == uid;
}

// On a worker: finding the process, its signature and its ancestors reads
// files, which has no place on the IO thread.
std::optional<ResolvedPeer> ResolveOnWorker(
    std::shared_ptr<const PeerResolver> resolver,
    PeerToken token) {
  return resolver->Resolve(token);
}

}  // namespace

ApprovalPresenter::~ApprovalPresenter() = default;

// One accepted connection: the socket, the peer it was resolved to and the
// Session that speaks for it. A Connection never destroys itself: Drop() closes
// the socket and asks the gateway to remove it from a fresh task, because it is
// usually reached from inside one of its own callbacks.
class Gateway::Connection final : public SessionDelegate {
 public:
  Connection(Gateway* gateway,
             ConnectionAdmission::Id id,
             std::unique_ptr<net::SocketPosix> socket)
      : gateway_(gateway),
        id_(id),
        socket_(std::move(socket)),
        created_(gateway->Now()) {}

  ~Connection() override {
    if (open_approval_ != 0) {
      gateway_->presenter_->Cancel(open_approval_);
    }
  }

  // Reads the peer's token while the descriptor is certainly this connection's,
  // and has the rest of the lookup done off the sequence. Nothing is read from
  // the connection until it has been resolved.
  void Start() {
    std::optional<PeerToken> token =
        gateway_->resolver_->TokenForSocket(socket_->socket_fd());
    if (!token) {
      Drop();
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
         base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
        base::BindOnce(&ResolveOnWorker, gateway_->resolver_,
                       std::move(*token)),
        base::BindOnce(&Connection::OnResolved, weak_factory_.GetWeakPtr()));
  }

  void CheckTimeouts() {
    if (dropped_) {
      return;
    }
    const base::TimeTicks now = gateway_->Now();
    if (!session_) {
      if (now - created_ > kResolveDeadline) {
        Drop();
      }
      return;
    }
    if (close_after_flush_) {
      if (now > flush_deadline_) {
        Drop();
      }
      return;
    }
    session_->CheckTimeouts();
    UpdateAdmission();
  }

  void OnClientRevoked(std::string_view client_id) {
    if (!dropped_ && session_) {
      session_->OnClientRevoked(client_id);
    }
  }

  // SessionDelegate:
  void Send(std::vector<uint8_t> frame) override { Enqueue(std::move(frame)); }

  void Close(CloseReason) override {
    // What was queued (the error that explains the close) is sent first.
    close_after_flush_ = true;
    flush_deadline_ = gateway_->Now() + kFlushDeadline;
    if (queue_.empty()) {
      Drop();
    }
  }

  void RequestApproval(const ApprovalRequest& request) override {
    open_approval_ = request.id;
    gateway_->presenter_->Show(
        request, base::BindOnce(&Connection::OnDecision,
                                weak_factory_.GetWeakPtr(), request.id));
  }

  bool PeerUnchanged() override {
    return peer_ && gateway_->resolver_->Unchanged(*peer_);
  }

 private:
  void OnResolved(std::optional<ResolvedPeer> peer) {
    if (dropped_) {
      return;
    }
    // Fail closed: nobody found, a process of another user, or no executable to
    // name, or a peer that has used up its failures.
    if (!peer || peer->identity.uid != gateway_->options_.uid ||
        peer->identity.process.path.empty() ||
        gateway_->limiter_.IsBlocked(PeerKey(peer->identity),
                                     gateway_->Now())) {
      Drop();
      return;
    }
    peer_ = std::move(*peer);
    session_ = std::make_unique<Session>(peer_->identity,
                                         gateway_->SessionEnv(), this);
    read_buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(kReadSize);
    StartReading();
  }

  void StartReading() {
    while (!dropped_ && !close_after_flush_) {
      const int result = socket_->ReadIfReady(
          read_buffer_.get(), kReadSize,
          base::BindOnce(&Connection::OnReadReady, weak_factory_.GetWeakPtr()));
      if (result == net::ERR_IO_PENDING) {
        return;
      }
      if (result <= 0) {  // end of stream, or an error
        Drop();
        return;
      }
      session_->OnBytesReceived(
          read_buffer_->first(static_cast<size_t>(result)));
      UpdateAdmission();
    }
  }

  void OnReadReady(int result) {
    if (dropped_) {
      return;
    }
    if (result != net::OK) {
      Drop();
      return;
    }
    StartReading();
  }

  void OnDecision(uint64_t request_id, ApprovalDecision decision) {
    if (dropped_ || !session_) {
      return;
    }
    open_approval_ = 0;
    session_->OnApprovalDecision(request_id, std::move(decision));
    UpdateAdmission();
  }

  void Enqueue(std::vector<uint8_t> frame) {
    if (dropped_) {
      return;
    }
    const size_t limit =
        authenticated_ ? kMaxQueuedBytesAuthenticated : kMaxQueuedBytesPreAuth;
    // A peer that does not read cannot make the browser hold what it was sent.
    if (queued_bytes_ + frame.size() > limit) {
      Drop();
      return;
    }
    queued_bytes_ += frame.size();
    const size_t size = frame.size();
    queue_.push_back(base::MakeRefCounted<net::DrainableIOBuffer>(
        base::MakeRefCounted<net::VectorIOBuffer>(std::move(frame)), size));
    if (!write_pending_) {
      WriteNext();
    }
  }

  void WriteNext() {
    while (!dropped_ && !write_pending_ && !queue_.empty()) {
      net::DrainableIOBuffer* front = queue_.front().get();
      const int result =
          socket_->Write(front, front->BytesRemaining(),
                         base::BindOnce(&Connection::OnWriteComplete,
                                        weak_factory_.GetWeakPtr()),
                         kTrafficAnnotation);
      if (result == net::ERR_IO_PENDING) {
        write_pending_ = true;
        return;
      }
      if (!AfterWrite(result)) {
        return;
      }
    }
    if (!dropped_ && close_after_flush_ && queue_.empty()) {
      Drop();
    }
  }

  void OnWriteComplete(int result) {
    write_pending_ = false;
    if (dropped_ || !AfterWrite(result)) {
      return;
    }
    WriteNext();
  }

  bool AfterWrite(int result) {
    if (result <= 0) {
      Drop();
      return false;
    }
    queued_bytes_ -= static_cast<size_t>(result);
    queue_.front()->DidConsume(result);
    if (queue_.front()->BytesRemaining() == 0) {
      queue_.pop_front();
    }
    return true;
  }

  // A connection that has proved who it is stops counting against the limit on
  // connections that have not.
  void UpdateAdmission() {
    if (!authenticated_ && session_ &&
        session_->state() == Session::State::kAuthenticated) {
      authenticated_ = true;
      gateway_->admission_.MarkAuthenticated(id_);
    }
  }

  void Drop() {
    if (dropped_) {
      return;
    }
    dropped_ = true;
    socket_->Close();
    gateway_->ScheduleRemoval(id_);
  }

  const raw_ptr<Gateway> gateway_;
  const ConnectionAdmission::Id id_;
  std::unique_ptr<net::SocketPosix> socket_;
  const base::TimeTicks created_;

  std::optional<ResolvedPeer> peer_;
  std::unique_ptr<Session> session_;
  scoped_refptr<net::IOBufferWithSize> read_buffer_;

  std::deque<scoped_refptr<net::DrainableIOBuffer>> queue_;
  size_t queued_bytes_ = 0;
  bool write_pending_ = false;

  bool authenticated_ = false;
  bool close_after_flush_ = false;
  base::TimeTicks flush_deadline_;
  uint64_t open_approval_ = 0;
  bool dropped_ = false;

  base::WeakPtrFactory<Connection> weak_factory_{this};
};

Gateway::Gateway(GatewayOptions options,
                 std::shared_ptr<const PeerResolver> resolver,
                 ClientStore* store,
                 ApprovalPresenter* presenter,
                 const base::TickClock* tick_clock,
                 const base::Clock* clock)
    : options_(std::move(options)),
      resolver_(std::move(resolver)),
      store_(store),
      presenter_(presenter),
      tick_clock_(tick_clock),
      clock_(clock) {}

Gateway::~Gateway() {
  Stop();
}

Gateway::StartResult Gateway::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (server_) {
    return StartResult::kAlreadyStarted;
  }
  if (!resolver_) {
    return StartResult::kNoPeerResolver;
  }
  switch (PrepareSocketPath(options_.socket_path, options_.uid)) {
    case SocketPrepareResult::kOk:
      break;
    case SocketPrepareResult::kInUse:
      return StartResult::kInUse;
    case SocketPrepareResult::kDirectoryUnsafe:
    case SocketPrepareResult::kNotOurs:
      return StartResult::kUnsafePath;
    case SocketPrepareResult::kError:
      return StartResult::kBindFailed;
  }
  auto server = std::make_unique<net::UnixDomainServerSocket>(
      base::BindRepeating(&AllowUser, options_.uid),
      /*use_abstract_namespace=*/false);
  if (server->BindAndListen(options_.socket_path.value(), kListenBacklog) !=
          net::OK ||
      !RestrictSocketPermissions(options_.socket_path, options_.uid)) {
    server.reset();
    base::DeleteFile(options_.socket_path);
    return StartResult::kBindFailed;
  }
  server_ = std::move(server);
  timer_.Start(FROM_HERE, base::Seconds(1), this, &Gateway::CheckTimeouts);
  DoAccept();
  return StartResult::kOk;
}

void Gateway::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  if (!server_) {
    return;
  }
  server_.reset();
  connections_.clear();
  admission_ = ConnectionAdmission();
  if (accepted_fd_ >= 0) {
    close(accepted_fd_);
    accepted_fd_ = -1;
  }
  base::DeleteFile(options_.socket_path);
}

void Gateway::OnClientRevoked(std::string_view client_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& [id, connection] : connections_) {
    connection->OnClientRevoked(client_id);
  }
}

void Gateway::CheckTimeouts() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& [id, connection] : connections_) {
    connection->CheckTimeouts();
  }
}

void Gateway::DoAccept() {
  while (server_) {
    const int result = server_->AcceptSocketDescriptor(
        &accepted_fd_,
        base::BindOnce(&Gateway::OnAcceptComplete, weak_factory_.GetWeakPtr()));
    if (result == net::ERR_IO_PENDING) {
      return;
    }
    if (!HandleAccepted(result)) {
      return;
    }
  }
}

void Gateway::OnAcceptComplete(int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (HandleAccepted(result)) {
    DoAccept();
  }
}

bool Gateway::HandleAccepted(int result) {
  if (result != net::OK) {
    // Out of descriptors or the like: try again later, not in a tight loop.
    LOG(WARNING) << "agent gateway: accept failed: "
                 << net::ErrorToString(result);
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Gateway::DoAccept, weak_factory_.GetWeakPtr()),
        base::Seconds(1));
    return false;
  }
  const int fd = accepted_fd_;
  accepted_fd_ = -1;
  AddConnection(fd);
  return true;
}

void Gateway::AddConnection(int fd) {
  base::ScopedFD owned(fd);
  const ConnectionAdmission::Id id = admission_.TryAdmit();
  if (id == ConnectionAdmission::kRefused) {
    return;  // closed unread by `owned`
  }
  auto socket = std::make_unique<net::SocketPosix>();
  if (socket->AdoptConnectedSocket(owned.release(), net::SockaddrStorage()) !=
      net::OK) {
    admission_.Release(id);
    return;
  }
  auto connection = std::make_unique<Connection>(this, id, std::move(socket));
  Connection* raw = connection.get();
  connections_[id] = std::move(connection);
  raw->Start();
}

void Gateway::ScheduleRemoval(ConnectionAdmission::Id id) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&Gateway::Remove, weak_factory_.GetWeakPtr(), id));
}

void Gateway::Remove(ConnectionAdmission::Id id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  connections_.erase(id);
  admission_.Release(id);
}

SessionEnvironment Gateway::SessionEnv() {
  SessionEnvironment env;
  env.store = store_;
  env.limiter = &limiter_;
  env.accepted_chains = &accepted_chains_;
  env.tick_clock = tick_clock_;
  env.clock = clock_;
  env.browser_uid = options_.uid;
  return env;
}

}  // namespace koan::agent
