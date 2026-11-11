// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_gateway.h"

#include <sys/socket.h>
#include <unistd.h>

#include <deque>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_frame.h"
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
// A loop that reads or accepts without waiting hands the thread back after this
// many turns, so that one busy peer cannot hold the IO thread.
constexpr int kMaxTurnsBeforeYielding = 8;
// How long to wait before accepting again after an error. Running out of
// descriptors does not fix itself in a moment; anything else is retried soon.
constexpr base::TimeDelta kAcceptRetryAfterResourceError = base::Seconds(1);
constexpr base::TimeDelta kAcceptRetryAfterError = base::Milliseconds(100);

// The kernel already refuses other users' connections to a socket in a 0700
// directory; this closes the accepted socket of any that gets through anyway.
// (UnixDomainServerSocket accepts the next connection by itself when this
// says no: a refused peer never reaches the gateway.)
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

// Says why a connection is being closed, in a frame without an id (nothing has
// been read from the peer, so there is no request to answer), so that a client
// finds out and can try again. Best effort, never waits: a fresh socket has
// room for this.
void WriteRefusal(int fd, std::string_view code, std::string_view message) {
  base::DictValue error;
  error.Set("code", -32000);
  error.Set("message", message);
  error.Set("data", code);
  base::DictValue root;
  root.Set("error", std::move(error));
  const std::optional<std::string> json = base::WriteJson(root);
  if (!json) {
    return;
  }
  const std::optional<std::vector<uint8_t>> frame =
      EncodeFrame(base::as_byte_span(*json));
  if (frame) {
    // Not checked: the peer may be gone already.
    (void)send(fd, frame->data(), frame->size(), MSG_DONTWAIT);
  }
}

}  // namespace

ApprovalPresenter::~ApprovalPresenter() = default;

GatewayObserver::~GatewayObserver() = default;

bool GatewayObserver::OnClientMessage(uint64_t connection_id,
                                      AgentMessage message) {
  return false;
}

GatewayOptions::GatewayOptions() = default;
GatewayOptions::GatewayOptions(const GatewayOptions&) = default;
GatewayOptions& GatewayOptions::operator=(const GatewayOptions&) = default;
GatewayOptions::~GatewayOptions() = default;

struct PreparedSocket {
  SocketPrepareResult result = SocketPrepareResult::kError;
  base::ScopedFD lock;
};

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
    // Every way a connection ends (closed by either side, Stop(), the gateway
    // destroyed) comes here.
    if (authenticated_) {
      if (GatewayObserver* observer = gateway_->options_.observer) {
        observer->OnClientDisconnected(id_);
      }
    }
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
      if (PeerHasHungUp(socket_->socket_fd())) {
        // `koan status` only checks that Koan answers: it is gone already.
        VLOG(1) << "agent gateway: a connection closed before it was looked at";
        Drop();
        return;
      }
      LOG(WARNING) << "agent gateway: no audit token for a connection";
      Refuse("Koan.peerUnresolved",
             "the connecting program could not be found");
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
        LOG(WARNING) << "agent gateway: a connection was not resolved in "
                     << kResolveDeadline.InSeconds() << " s; closing it";
        Refuse("Koan.peerUnresolved",
               "looking up the connecting program took too long");
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

  void Close(CloseReason) override { CloseAfterFlush(); }

  void RequestApproval(const ApprovalRequest& request) override {
    open_approval_ = request.id;
    // The dialog is not on this sequence: whoever answers, the answer is posted
    // back here, and does nothing if this connection has gone by then.
    gateway_->presenter_->Show(
        request,
        base::BindPostTaskToCurrentDefault(base::BindOnce(
            &Connection::OnDecision, weak_factory_.GetWeakPtr(), request.id)));
  }

  bool PeerUnchanged() override {
    return peer_ && gateway_->resolver_->Unchanged(*peer_);
  }

  bool Forward(AgentMessage message) override {
    // The observer hears that the client is connected before its first
    // message: the indicator shows before anything reaches a tab.
    UpdateAdmission();
    GatewayObserver* observer = gateway_->options_.observer;
    return authenticated_ && observer &&
           observer->OnClientMessage(id_, std::move(message));
  }

  // An answer or event from the facade.
  void SendJson(std::string_view json) {
    if (dropped_ || close_after_flush_ || !authenticated_) {
      return;
    }
    std::optional<std::vector<uint8_t>> frame =
        EncodeFrame(base::as_byte_span(json));
    if (frame) {
      Enqueue(std::move(*frame));
    }
  }

 private:
  void OnResolved(std::optional<ResolvedPeer> peer) {
    if (dropped_) {
      return;
    }
    if (!peer) {
      LOG(WARNING) << "agent gateway: a connecting program could not be "
                      "resolved; closing the connection";
      Refuse("Koan.peerUnresolved",
             "the connecting program could not be found");
      return;
    }
    // Fail closed: a process of another user, or no executable to name, or a
    // peer that has used up its failures. None of these is told anything.
    if (peer->identity.uid != gateway_->options_.uid ||
        peer->identity.process.path.empty() ||
        gateway_->limiter_.IsBlocked(PeerKey(peer->identity),
                                     gateway_->Now())) {
      Drop();
      return;
    }
    // One program may not hold every slot reserved for connections that have
    // not authenticated.
    if (!gateway_->admission_.AssignPeer(id_, PeerKey(peer->identity))) {
      LOG(WARNING) << "agent gateway: too many unauthenticated connections "
                      "from "
                   << peer->identity.process.path;
      Refuse("Koan.busy", "too many connections from this program; retry");
      return;
    }
    peer_ = std::move(*peer);
    session_ = std::make_unique<Session>(peer_->identity,
                                         gateway_->SessionEnv(), this);
    read_buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(kReadSize);
    StartReading();
  }

  void StartReading() {
    for (int turn = 0; !dropped_ && !close_after_flush_; ++turn) {
      if (turn == kMaxTurnsBeforeYielding) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&Connection::StartReading,
                                      weak_factory_.GetWeakPtr()));
        return;
      }
      const int result = socket_->ReadIfReady(
          read_buffer_.get(), kReadSize,
          base::BindOnce(&Connection::OnReadReady, weak_factory_.GetWeakPtr()));
      if (result == net::ERR_IO_PENDING) {
        return;
      }
      if (result == 0 && !queue_.empty() && open_approval_ == 0) {
        // The peer is done sending but has not got everything it was sent (it
        // half-closed after a request): let the reply go out, then close. With
        // a prompt open nobody is left to answer it.
        CloseAfterFlush();
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

  // What was queued (the error that explains the close) is sent first, and
  // nothing more is read.
  void CloseAfterFlush() {
    if (dropped_ || close_after_flush_) {
      return;
    }
    close_after_flush_ = true;
    flush_deadline_ = gateway_->Now() + kFlushDeadline;
    if (queue_.empty()) {
      Drop();
    }
  }

  // A connection that has proved who it is stops counting against the limit on
  // connections that have not.
  void UpdateAdmission() {
    if (!authenticated_ && session_ &&
        session_->state() == Session::State::kAuthenticated) {
      authenticated_ = true;
      gateway_->admission_.MarkAuthenticated(id_);
      if (GatewayObserver* observer = gateway_->options_.observer) {
        const std::string& client_id = session_->client_id();
        const std::optional<ClientRecord> record =
            gateway_->store_->Find(client_id);
        observer->OnClientConnected(id_, client_id,
                                    record ? record->label : std::string());
      }
    }
  }

  // Tells the peer why, once, and closes.
  void Refuse(std::string_view code, std::string_view message) {
    if (dropped_) {
      return;
    }
    WriteRefusal(socket_->socket_fd(), code, message);
    Drop();
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

namespace {

// On a thread that may block.
PreparedSocket PrepareOnWorker(base::FilePath socket_path, uid_t uid) {
  PreparedSocket prepared;
  prepared.result = PrepareSocketPath(socket_path, uid, &prepared.lock);
  return prepared;
}

}  // namespace

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
      clock_(clock),
      limiter_(options_.limits) {
  // Created anywhere, used and destroyed on one sequence.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

Gateway::~Gateway() {
  // A destroyed gateway does not report back.
  start_done_.Reset();
  Stop();
}

void Gateway::Start(base::OnceCallback<void(StartResult)> done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kStopped) {
    std::move(done).Run(StartResult::kAlreadyStarted);
    return;
  }
  if (!resolver_) {
    std::move(done).Run(StartResult::kNoPeerResolver);
    return;
  }
  state_ = State::kStarting;
  start_done_ = std::move(done);
  if (outstanding_preparations_ > 0) {
    // An abandoned preparation still holds the lock: wait for it to come back.
    start_waiting_ = true;
    return;
  }
  BeginPrepare();
}

void Gateway::BeginPrepare() {
  ++outstanding_preparations_;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&PrepareOnWorker, options_.socket_path, options_.uid),
      base::BindOnce(&Gateway::OnPrepared, prepare_weak_factory_.GetWeakPtr(),
                     generation_));
}

void Gateway::OnPrepared(uint64_t generation, PreparedSocket prepared) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  --outstanding_preparations_;
  if (generation != generation_ || state_ != State::kStarting) {
    // Abandoned by a Stop(): `prepared` goes out of scope here and lets go of
    // its lock; a Start() that came in meanwhile has been waiting for that.
    if (start_waiting_ && outstanding_preparations_ == 0) {
      start_waiting_ = false;
      BeginPrepare();
    }
    return;
  }
  switch (prepared.result) {
    case SocketPrepareResult::kOk:
      break;
    case SocketPrepareResult::kInUse:
      return FinishStart(StartResult::kInUse);
    case SocketPrepareResult::kDirectoryUnsafe:
    case SocketPrepareResult::kNotOurs:
      return FinishStart(StartResult::kUnsafePath);
    case SocketPrepareResult::kError:
      return FinishStart(StartResult::kBindFailed);
  }
  // The lock is ours: nothing else is bound here, and a bind that fails leaves
  // nothing of ours at the path, so nothing is removed on that path.
  auto server = std::make_unique<net::UnixDomainServerSocket>(
      base::BindRepeating(
          &AllowUser, options_.kernel_uid_for_testing.value_or(options_.uid)),
      /*use_abstract_namespace=*/false);
  if (server->BindAndListen(options_.socket_path.value(), kListenBacklog) !=
      net::OK) {
    return FinishStart(StartResult::kBindFailed);
  }
  const std::optional<SocketIdentity> identity =
      SocketIdentityOf(options_.socket_path, options_.uid);
  if (!identity ||
      !RestrictSocketPermissions(options_.socket_path, options_.uid)) {
    if (identity) {
      RemoveSocketIfSame(options_.socket_path, *identity);
    }
    return FinishStart(StartResult::kBindFailed);
  }
  server_ = std::move(server);
  lock_ = std::move(prepared.lock);
  socket_identity_ = identity;
  state_ = State::kStarted;
  timer_.Start(FROM_HERE, base::Seconds(1), this, &Gateway::CheckTimeouts);
  DoAccept();
  FinishStart(StartResult::kOk);
}

void Gateway::FinishStart(StartResult result) {
  if (result != StartResult::kOk) {
    state_ = State::kStopped;
  }
  if (start_done_) {
    std::move(start_done_).Run(result);
  }
}

void Gateway::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  ++generation_;
  start_waiting_ = false;
  const State was = state_;
  state_ = State::kStopped;
  connections_.clear();
  admission_ = ConnectionAdmission();
  server_.reset();
  if (accepted_fd_ >= 0) {
    close(accepted_fd_);
    accepted_fd_ = -1;
  }
  // Only the socket this gateway bound, and only if it is still the file at the
  // path: after a crash and a restart, or a race, the path may be another
  // browser's.
  if (socket_identity_) {
    RemoveSocketIfSame(options_.socket_path, *socket_identity_);
    socket_identity_.reset();
  }
  lock_.reset();
  if (was == State::kStarting && start_done_) {
    std::move(start_done_).Run(StartResult::kStopped);
  }
}

void Gateway::OnClientRevoked(std::string_view client_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& [id, connection] : connections_) {
    connection->OnClientRevoked(client_id);
  }
}

void Gateway::SendToClient(uint64_t connection_id, std::string_view json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    it->second->SendJson(json);
  }
}

void Gateway::CheckTimeouts() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& [id, connection] : connections_) {
    connection->CheckTimeouts();
  }
}

void Gateway::DoAccept() {
  for (int turn = 0; server_; ++turn) {
    if (turn == kMaxTurnsBeforeYielding) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&Gateway::DoAccept, weak_factory_.GetWeakPtr()));
      return;
    }
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
    LOG(WARNING) << "agent gateway: accept failed: "
                 << net::ErrorToString(result);
    // Out of descriptors or memory: try again later, not in a tight loop. Any
    // other error is not going to be fixed by waiting, but is not a reason to
    // spin either.
    const bool resources = result == net::ERR_INSUFFICIENT_RESOURCES ||
                           result == net::ERR_OUT_OF_MEMORY;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Gateway::DoAccept, weak_factory_.GetWeakPtr()),
        resources ? kAcceptRetryAfterResourceError : kAcceptRetryAfterError);
    return false;
  }
  const int fd = accepted_fd_;
  accepted_fd_ = -1;
  AddConnection(fd);
  return true;
}

void Gateway::AddConnection(int fd) {
  base::ScopedFD owned(fd);
  // A descriptor that a program we start would inherit is a way to the socket.
  if (!base::SetCloseOnExec(owned.get())) {
    return;
  }
  const ConnectionAdmission::Id id = admission_.TryAdmit();
  if (id == ConnectionAdmission::kRefused) {
    LOG(WARNING) << "agent gateway: too many connections; refusing one";
    WriteRefusal(owned.get(), "Koan.busy", "too many connections; retry");
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

void Gateway::SetLimits(const RateLimiter::Limits& limits) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  limiter_.SetLimits(limits);
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
