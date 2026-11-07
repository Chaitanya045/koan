// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_gateway.h"

#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/thread_pool.h"
#include "base/test/run_until.h"
#include "base/test/simple_test_clock.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/socket/unix_domain_client_socket_posix.h"
#include "net/socket/unix_domain_server_socket_posix.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace koan::agent {

namespace {

// Stands in for the OS: who the peer is, whether it is still the same, whether
// the lookup works at all. Resolve() runs on a worker, so what it reads is
// locked, and it can be made to wait to stand for a slow lookup.
class FakeResolver final : public PeerResolver {
 public:
  FakeResolver() {
    identity_.uid = getuid();
    identity_.process = {"/Applications/Koan.app/Contents/MacOS/koan",
                         "team:K1"};
    identity_.ancestors = {{"/bin/zsh", "apple"},
                           {"/usr/local/bin/claude", "team:ANTHROPIC"}};
  }

  std::optional<PeerToken> TokenForSocket(int) const override {
    if (no_token) {
      return std::nullopt;
    }
    return PeerToken{1, 2, 3};
  }
  std::optional<ResolvedPeer> Resolve(const PeerToken&) const override {
    if (block) {
      base::ScopedAllowBaseSyncPrimitivesForTesting allow;
      gate_.Wait();
    }
    if (fail_resolve) {
      return std::nullopt;
    }
    base::AutoLock lock(lock_);
    ResolvedPeer peer;
    peer.identity = identity_;
    peer.handle = {9};
    return peer;
  }
  bool Unchanged(const ResolvedPeer&) const override { return unchanged; }

  PeerIdentity identity() const {
    base::AutoLock lock(lock_);
    return identity_;
  }
  void SetIdentity(PeerIdentity value) {
    base::AutoLock lock(lock_);
    identity_ = std::move(value);
  }
  void Release() { gate_.Signal(); }

  std::atomic<bool> no_token{false};
  std::atomic<bool> fail_resolve{false};
  std::atomic<bool> unchanged{true};
  std::atomic<bool> block{false};

 private:
  mutable base::Lock lock_;
  PeerIdentity identity_;
  mutable base::WaitableEvent gate_{
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED};
};

class FakePresenter final : public ApprovalPresenter {
 public:
  void Show(const ApprovalRequest& request,
            base::OnceCallback<void(ApprovalDecision)> answer) override {
    requests.push_back(request);
    answers.push_back(std::move(answer));
  }
  void Cancel(uint64_t id) override { cancelled.push_back(id); }

  // The user answers the latest prompt.
  void Answer(bool approve) {
    ApprovalDecision decision;
    decision.approved = approve;
    std::move(answers.back()).Run(decision);
    answers.pop_back();
  }

  std::vector<ApprovalRequest> requests;
  std::vector<base::OnceCallback<void(ApprovalDecision)>> answers;
  std::vector<uint64_t> cancelled;
};

struct Key {
  Key() { ED25519_keypair(public_key.data(), private_key.data()); }
  std::string Sign(const std::vector<uint8_t>& message) const {
    std::array<uint8_t, kSignatureSize> signature;
    EXPECT_EQ(ED25519_sign(signature.data(), message.data(), message.size(),
                           private_key.data()),
              1);
    return base::Base64Encode(signature);
  }
  std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> public_key;
  std::array<uint8_t, ED25519_PRIVATE_KEY_LEN> private_key;
};

// A client of the gateway over a real unix socket.
class TestClient {
 public:
  explicit TestClient(const base::FilePath& path)
      : socket_(path.value(), /*use_abstract_namespace=*/false) {}

  bool Connect() {
    net::TestCompletionCallback callback;
    return callback.GetResult(socket_.Connect(callback.callback())) == net::OK;
  }

  void Send(const std::string& json) {
    SendBytes(*EncodeFrame(base::as_byte_span(json)));
  }
  void SendBytes(base::span<const uint8_t> bytes) {
    // Not checked: the server closes some of these connections before it reads
    // them, and a write to one that has gone is expected to fail.
    auto buffer = base::MakeRefCounted<net::VectorIOBuffer>(bytes);
    net::TestCompletionCallback callback;
    callback.GetResult(
        socket_.Write(buffer.get(), static_cast<int>(bytes.size()),
                      callback.callback(), TRAFFIC_ANNOTATION_FOR_TESTS));
  }

  // The next reply, or nullopt at the end of the stream.
  std::optional<base::DictValue> Read() {
    while (true) {
      if (auto frame = decoder_.TakeFrame()) {
        return base::JSONReader::ReadDict(
            std::string_view(reinterpret_cast<const char*>(frame->data()),
                             frame->size()),
            base::JSON_PARSE_RFC);
      }
      auto buffer = base::MakeRefCounted<net::IOBufferWithSize>(4096);
      net::TestCompletionCallback callback;
      const int result = callback.GetResult(
          socket_.Read(buffer.get(), 4096, callback.callback()));
      if (result <= 0) {
        return std::nullopt;
      }
      EXPECT_TRUE(decoder_.Append(buffer->first(static_cast<size_t>(result))));
    }
  }

  // Whether the server closes the connection (reading until it does).
  bool WaitForClose() {
    while (Read()) {
    }
    return true;
  }

 private:
  net::UnixDomainClientSocket socket_;
  FrameDecoder decoder_{kMaxPayload};
};

std::string ErrorCode(const std::optional<base::DictValue>& reply) {
  if (!reply) {
    return "<closed>";
  }
  const base::DictValue* error = reply->FindDict("error");
  return error ? *error->FindString("data") : std::string();
}

}  // namespace

class KoanAgentGatewayTest : public testing::Test {
 protected:
  void SetUp() override {
    ticks_.SetNowTicks(base::TimeTicks() + base::Seconds(1000));
    wall_.SetNow(base::Time::FromSecondsSinceUnixEpoch(1700000000));
    // sun_path is short: TMPDIR on macOS is not.
    ASSERT_TRUE(dir_.CreateUniqueTempDirUnderPath(base::FilePath("/tmp")));
    path_ = dir_.GetPath().Append("AgentControl").Append("agent.sock");
    resolver_ = std::make_shared<FakeResolver>();
    MakeGateway();
  }

  void MakeGateway() {
    GatewayOptions options;
    options.socket_path = path_;
    options.uid = getuid();
    gateway_ = std::make_unique<Gateway>(options, resolver_, &store_,
                                         &presenter_, &ticks_, &wall_);
    ASSERT_EQ(StartAndWait(*gateway_), Gateway::StartResult::kOk);
  }

  static Gateway::StartResult StartAndWait(Gateway& gateway) {
    base::test::TestFuture<Gateway::StartResult> result;
    gateway.Start(result.GetCallback());
    return result.Get();
  }

  // A resolver answer naming a program at `path`, for tests that need several
  // programs (one program may hold only two unauthenticated slots).
  void SetProgram(const std::string& path) {
    PeerIdentity identity = resolver_->identity();
    identity.process.path = path;
    resolver_->SetIdentity(identity);
  }

  std::unique_ptr<TestClient> Connect() {
    auto client = std::make_unique<TestClient>(path_);
    EXPECT_TRUE(client->Connect());
    return client;
  }

  // A connection the server has resolved and is serving: it answers. (A nonce
  // for an id is asked for, which is not a failure.) `program` is the
  // executable the fake OS reports for it.
  std::unique_ptr<TestClient> LiveConnection(const std::string& program = "") {
    if (!program.empty()) {
      SetProgram(program);
    }
    auto client = Connect();
    client->Send(AuthenticateFrame(std::string(32, 'a')));
    auto reply = client->Read();
    EXPECT_TRUE(reply && reply->FindDict("result"));
    return client;
  }

  std::string PairFrame(const Key& key, int id = 1) {
    const std::string label = "Claude Code";
    const std::vector<uint8_t> message = PairingMessage(key.public_key, label);
    return R"({"id":)" + std::to_string(id) +
           R"(,"method":"Koan.pair","params":{"publicKey":")" +
           base::Base64Encode(key.public_key) + R"(","label":")" + label +
           R"(","proof":")" + key.Sign(message) + R"("}})";
  }

  // Pairs `key` through the gateway and returns the client id.
  std::string Pair(const Key& key) {
    auto client = Connect();
    client->Send(PairFrame(key));
    EXPECT_TRUE(
        base::test::RunUntil([&] { return !presenter_.answers.empty(); }));
    presenter_.Answer(true);
    auto reply = client->Read();
    EXPECT_TRUE(reply);
    return *reply->FindDict("result")->FindString("clientId");
  }

  std::string AuthenticateFrame(const std::string& id) {
    return R"({"id":10,"method":"Koan.authenticate","params":{"clientId":")" +
           id + R"("}})";
  }
  std::string ProofFrame(const Key& key,
                         const std::string& id,
                         const std::string& nonce_b64) {
    return R"({"id":11,"method":"Koan.authenticate","params":{"clientId":")" +
           id + R"(","signature":")" +
           key.Sign(ProofMessage(id, *base::Base64Decode(nonce_b64))) +
           R"("}})";
  }

  // Authenticated over a real socket.
  std::unique_ptr<TestClient> Authenticated(const Key& key,
                                            const std::string& id) {
    auto client = Connect();
    client->Send(AuthenticateFrame(id));
    auto nonce = client->Read();
    EXPECT_TRUE(nonce);
    client->Send(
        ProofFrame(key, id, *nonce->FindDict("result")->FindString("nonce")));
    auto reply = client->Read();
    EXPECT_TRUE(reply);
    EXPECT_EQ(reply->FindDict("result")->FindBool("authenticated"), true);
    return client;
  }

  void TearDown() override { resolver_->Release(); }

  bool WaitForConnections(size_t count) {
    return base::test::RunUntil(
        [&] { return gateway_->connection_count() == count; });
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  base::SimpleTestTickClock ticks_;
  base::SimpleTestClock wall_;
  base::ScopedTempDir dir_;
  base::FilePath path_;
  std::shared_ptr<FakeResolver> resolver_;
  InMemoryClientStore store_;
  FakePresenter presenter_;
  std::unique_ptr<Gateway> gateway_;
};

TEST_F(KoanAgentGatewayTest, PairsAndAuthenticatesOverARealSocket) {
  Key key;
  auto pairing = Connect();
  pairing->Send(PairFrame(key));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !presenter_.requests.empty(); }));
  // The dialog describes the peer as the OS reported it.
  EXPECT_EQ(presenter_.requests[0].kind, ApprovalKind::kNewClient);
  EXPECT_EQ(presenter_.requests[0].peer.process.path,
            "/Applications/Koan.app/Contents/MacOS/koan");
  EXPECT_EQ(presenter_.requests[0].peer.ancestors.size(), 2u);
  presenter_.Answer(true);
  auto reply = pairing->Read();
  ASSERT_TRUE(reply);
  const std::string id = *reply->FindDict("result")->FindString("clientId");

  auto client = Authenticated(key, id);
  client->Send(R"({"id":20,"method":"Koan.status"})");
  auto status = client->Read();
  ASSERT_TRUE(status);
  EXPECT_EQ(*status->FindDict("result")->FindString("clientId"), id);
  // Nothing else is served.
  client->Send(R"({"id":21,"method":"Page.navigate","params":{}})");
  EXPECT_EQ(ErrorCode(client->Read()), "Koan.unavailable");
}

TEST_F(KoanAgentGatewayTest, AConnectionOfAnotherUserIsClosedUnread) {
  PeerIdentity stranger = resolver_->identity();
  stranger.uid = getuid() + 1;
  resolver_->SetIdentity(stranger);
  auto client = Connect();
  client->Send(R"({"id":1,"method":"Koan.status"})");
  EXPECT_TRUE(client->WaitForClose());
  EXPECT_TRUE(WaitForConnections(0));
}

TEST_F(KoanAgentGatewayTest, APeerThatCannotBeFoundIsToldAndClosedUnread) {
  for (const bool no_token : {true, false}) {
    resolver_->no_token = no_token;
    resolver_->fail_resolve = !no_token;
    auto client = Connect();
    client->Send(R"({"id":1,"method":"Koan.status"})");
    // The client learns why (with no id: nothing of its was read), then EOF.
    EXPECT_EQ(ErrorCode(client->Read()), "Koan.peerUnresolved")
        << "token: " << no_token;
    EXPECT_FALSE(client->Read());
    EXPECT_TRUE(WaitForConnections(0));
  }
}

TEST_F(KoanAgentGatewayTest, APeerWithNoExecutablePathIsClosed) {
  PeerIdentity nameless = resolver_->identity();
  nameless.process.path.clear();
  resolver_->SetIdentity(nameless);
  auto client = Connect();
  EXPECT_TRUE(client->WaitForClose());
}

TEST_F(KoanAgentGatewayTest, OnlyFourConnectionsMaySayNothing) {
  std::vector<std::unique_ptr<TestClient>> held;
  for (size_t i = 0; i < ConnectionAdmission::kMaxUnauthenticated; ++i) {
    held.push_back(LiveConnection("/tmp/program" + std::to_string(i)));
  }
  // The fifth is accepted by the kernel and told to come back by the gateway.
  auto fifth = Connect();
  EXPECT_EQ(ErrorCode(fifth->Read()), "Koan.busy");
  EXPECT_FALSE(fifth->Read());
  EXPECT_TRUE(WaitForConnections(ConnectionAdmission::kMaxUnauthenticated));
  // A slot comes back when a connection goes.
  held.pop_back();
  ASSERT_TRUE(WaitForConnections(ConnectionAdmission::kMaxUnauthenticated - 1));
  auto again = LiveConnection("/tmp/another");
  EXPECT_EQ(gateway_->connection_count(),
            ConnectionAdmission::kMaxUnauthenticated);
}

TEST_F(KoanAgentGatewayTest, OneProgramCannotTakeEveryUnauthenticatedSlot) {
  auto first = LiveConnection("/tmp/greedy");
  auto second = LiveConnection("/tmp/greedy");
  // The third from the same program is told so, once it is known who it is.
  SetProgram("/tmp/greedy");
  auto third = Connect();
  EXPECT_EQ(ErrorCode(third->Read()), "Koan.busy");
  EXPECT_FALSE(third->Read());
  ASSERT_TRUE(WaitForConnections(2));
  // Another program still gets in, and the first two are unharmed.
  auto other = LiveConnection("/tmp/polite");
  first->Send(AuthenticateFrame(std::string(32, 'b')));
  EXPECT_TRUE(first->Read());
}

TEST_F(KoanAgentGatewayTest, AuthenticatedConnectionsDoNotCountAsSilentOnes) {
  Key key;
  const std::string id = Pair(key);
  std::vector<std::unique_ptr<TestClient>> held;
  // Four authenticated from one program: they do not count against its two.
  for (size_t i = 0; i < ConnectionAdmission::kMaxUnauthenticated; ++i) {
    held.push_back(Authenticated(key, id));
  }
  // Four authenticated, and four more unauthenticated still fit (8 in all).
  for (size_t i = 0; i < ConnectionAdmission::kMaxUnauthenticated; ++i) {
    held.push_back(LiveConnection("/tmp/silent" + std::to_string(i)));
  }
  auto ninth = Connect();
  EXPECT_EQ(ErrorCode(ninth->Read()), "Koan.busy");
}

TEST_F(KoanAgentGatewayTest, ASilentConnectionIsClosedAfterTheDeadline) {
  auto client = LiveConnection();
  ticks_.Advance(Session::kPreAuthDeadline + base::Seconds(1));
  gateway_->CheckTimeouts();
  EXPECT_TRUE(client->WaitForClose());
  EXPECT_TRUE(WaitForConnections(0));
}

TEST_F(KoanAgentGatewayTest,
       AConnectionThatNeverFinishesBeingResolvedIsClosed) {
  resolver_->block = true;  // the lookup hangs
  auto client = Connect();
  ASSERT_TRUE(WaitForConnections(1));
  ticks_.Advance(Gateway::kResolveDeadline + base::Seconds(1));
  gateway_->CheckTimeouts();
  // Told why, rather than left to wonder.
  EXPECT_EQ(ErrorCode(client->Read()), "Koan.peerUnresolved");
  EXPECT_FALSE(client->Read());
  EXPECT_TRUE(WaitForConnections(0));
  resolver_->Release();  // the lookup finally returns, to nobody
}

TEST_F(KoanAgentGatewayTest, AStalledFrameClosesTheConnection) {
  auto client = Connect();
  // A whole request and, in the same write, the start of another: once the
  // reply to the first is back, the server has seen the half frame.
  std::vector<uint8_t> bytes =
      *EncodeFrame(base::as_byte_span(AuthenticateFrame(std::string(32, 'a'))));
  const std::vector<uint8_t> next =
      *EncodeFrame(base::as_byte_span(AuthenticateFrame(std::string(32, 'b'))));
  bytes.insert(bytes.end(), next.begin(), next.begin() + 6);
  client->SendBytes(bytes);
  ASSERT_TRUE(client->Read());
  ticks_.Advance(Session::kFrameTimeout + base::Seconds(1));
  gateway_->CheckTimeouts();
  EXPECT_TRUE(client->WaitForClose());
}

TEST_F(KoanAgentGatewayTest, GarbageCountsAgainstThePeerAcrossConnections) {
  for (int i = 0; i < RateLimiter::kMaxFailures; ++i) {
    auto client = Connect();
    client->SendBytes(std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff});
    EXPECT_TRUE(client->WaitForClose()) << i;
    ASSERT_TRUE(WaitForConnections(0));
  }
  EXPECT_TRUE(gateway_->limiter_for_testing().IsBlocked(
      PeerKey(resolver_->identity()), ticks_.NowTicks()));
  // The blocked peer is closed as soon as it is resolved, without a reply.
  auto client = Connect();
  client->Send(AuthenticateFrame(std::string(32, 'a')));
  EXPECT_FALSE(client->Read());
}

TEST_F(KoanAgentGatewayTest, ThePeerIsLookedAtAgainBeforeAProofIsVerified) {
  Key key;
  const std::string id = Pair(key);
  auto client = Connect();
  client->Send(AuthenticateFrame(id));
  auto nonce = client->Read();
  ASSERT_TRUE(nonce);
  resolver_->unchanged = false;  // it exec'd something else
  client->Send(
      ProofFrame(key, id, *nonce->FindDict("result")->FindString("nonce")));
  EXPECT_FALSE(client->Read());  // closed, and not authenticated
}

TEST_F(KoanAgentGatewayTest, ThePeerIsLookedAtAgainBeforeAnApprovalIsApplied) {
  Key key;
  auto client = Connect();
  client->Send(PairFrame(key));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !presenter_.answers.empty(); }));
  resolver_->unchanged = false;  // while the dialog was open
  presenter_.Answer(true);
  EXPECT_EQ(ErrorCode(client->Read()), "Koan.peerChanged");
  EXPECT_FALSE(client->Read());
  EXPECT_EQ(store_.size(), 0u);
}

TEST_F(KoanAgentGatewayTest, ARevokedClientsConnectionsCloseAtOnce) {
  Key key;
  const std::string id = Pair(key);
  auto client = Authenticated(key, id);
  ASSERT_TRUE(store_.Remove(id));
  gateway_->OnClientRevoked(id);
  EXPECT_TRUE(client->WaitForClose());
  EXPECT_TRUE(WaitForConnections(0));
}

TEST_F(KoanAgentGatewayTest, APromptIsCancelledWhenItsConnectionGoes) {
  Key key;
  auto client = Connect();
  client->Send(PairFrame(key));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !presenter_.requests.empty(); }));
  client.reset();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !presenter_.cancelled.empty(); }));
  EXPECT_EQ(presenter_.cancelled[0], presenter_.requests[0].id);
  // And the slot for prompts is free again.
  ASSERT_TRUE(WaitForConnections(0));
  auto next = Connect();
  next->Send(PairFrame(key));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return presenter_.requests.size() == 2; }));
}

TEST_F(KoanAgentGatewayTest, StoppingClosesEverythingAndRemovesTheSocket) {
  auto client = LiveConnection();
  ASSERT_TRUE(base::PathExists(path_));
  gateway_->Stop();
  EXPECT_FALSE(gateway_->started());
  EXPECT_FALSE(base::PathExists(path_));
  EXPECT_TRUE(client->WaitForClose());
  EXPECT_EQ(gateway_->connection_count(), 0u);
}

TEST_F(KoanAgentGatewayTest, TheSocketIsPrivate) {
  struct stat info;
  ASSERT_EQ(lstat(path_.value().c_str(), &info), 0);
  EXPECT_TRUE(S_ISSOCK(info.st_mode));
  EXPECT_EQ(info.st_mode & 0777, 0600u);
  ASSERT_EQ(lstat(path_.DirName().value().c_str(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0700u);
}

TEST_F(KoanAgentGatewayTest, StartRefusesWhatItShouldNot) {
  // A second gateway on a live socket.
  GatewayOptions options;
  options.socket_path = path_;
  options.uid = getuid();
  Gateway second(options, resolver_, &store_, &presenter_, &ticks_, &wall_);
  EXPECT_EQ(StartAndWait(second), Gateway::StartResult::kInUse);
  EXPECT_TRUE(base::PathExists(path_));  // the first one's socket is untouched
  // A refused gateway that is stopped or destroyed leaves it alone as well.
  second.Stop();
  EXPECT_TRUE(base::PathExists(path_));
  auto client = LiveConnection();  // and the first still serves
  // No way to find out who connects.
  Gateway blind(options, nullptr, &store_, &presenter_, &ticks_, &wall_);
  EXPECT_EQ(StartAndWait(blind), Gateway::StartResult::kNoPeerResolver);
  // Starting twice.
  EXPECT_EQ(StartAndWait(*gateway_), Gateway::StartResult::kAlreadyStarted);
  // A directory that is a symlink.
  const base::FilePath real = dir_.GetPath().Append("real");
  const base::FilePath link = dir_.GetPath().Append("link");
  ASSERT_TRUE(base::CreateDirectory(real));
  ASSERT_TRUE(base::CreateSymbolicLink(real, link));
  options.socket_path = link.Append("agent.sock");
  Gateway linked(options, resolver_, &store_, &presenter_, &ticks_, &wall_);
  EXPECT_EQ(StartAndWait(linked), Gateway::StartResult::kUnsafePath);
}

TEST_F(KoanAgentGatewayTest, TwoGatewaysStartingTogetherLetOneWin) {
  gateway_->Stop();
  GatewayOptions options;
  options.socket_path = path_;
  options.uid = getuid();
  Gateway a(options, resolver_, &store_, &presenter_, &ticks_, &wall_);
  Gateway b(options, resolver_, &store_, &presenter_, &ticks_, &wall_);
  base::test::TestFuture<Gateway::StartResult> first, second;
  a.Start(first.GetCallback());
  b.Start(second.GetCallback());
  const Gateway::StartResult ra = first.Get();
  const Gateway::StartResult rb = second.Get();
  EXPECT_TRUE(
      (ra == Gateway::StartResult::kOk && rb == Gateway::StartResult::kInUse) ||
      (rb == Gateway::StartResult::kOk && ra == Gateway::StartResult::kInUse))
      << static_cast<int>(ra) << " " << static_cast<int>(rb);
  // And the one that won is reachable.
  auto client = std::make_unique<TestClient>(path_);
  EXPECT_TRUE(client->Connect());
}

TEST_F(KoanAgentGatewayTest, StoppingLeavesAnotherBrowsersSocketAlone) {
  // The path now names a different socket (another browser's, after this one
  // lost its lock in some way nobody foresaw): stopping must not remove it.
  const base::FilePath moved = dir_.GetPath().Append("moved.sock");
  ASSERT_EQ(rename(path_.value().c_str(), moved.value().c_str()), 0);
  net::UnixDomainServerSocket other(
      base::BindRepeating(
          [](const net::UnixDomainServerSocket::Credentials&) { return true; }),
      false);
  ASSERT_EQ(other.BindAndListen(path_.value(), 1), net::OK);
  gateway_->Stop();
  EXPECT_TRUE(base::PathExists(path_));
}

TEST_F(KoanAgentGatewayTest, StoppingBeforeTheStartHasFinishedReportsStopped) {
  gateway_->Stop();
  base::test::TestFuture<Gateway::StartResult> result;
  gateway_->Start(result.GetCallback());
  gateway_->Stop();
  EXPECT_EQ(result.Get(), Gateway::StartResult::kStopped);
  EXPECT_FALSE(gateway_->started());
  // The abandoned preparation lets go of its lock once it has run; only then
  // is a new start sure to find the path free.
  task_environment_.RunUntilIdle();
  // And it can be started again afterwards.
  EXPECT_EQ(StartAndWait(*gateway_), Gateway::StartResult::kOk);
  auto client = LiveConnection();
}

TEST_F(KoanAgentGatewayTest, StoppingWhileALookupIsInFlightIsSafe) {
  resolver_->block = true;
  auto client = Connect();
  ASSERT_TRUE(WaitForConnections(1));
  gateway_->Stop();
  EXPECT_EQ(gateway_->connection_count(), 0u);
  EXPECT_TRUE(client->WaitForClose());
  resolver_->Release();  // the worker finishes; its answer finds nobody
  task_environment_.RunUntilIdle();
}

TEST_F(KoanAgentGatewayTest, AnAnswerFromAnotherThreadReachesTheConnection) {
  Key key;
  auto client = Connect();
  client->Send(PairFrame(key));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !presenter_.answers.empty(); }));
  // The dialog is on another thread and answers from there.
  ApprovalDecision decision;
  decision.approved = true;
  base::ThreadPool::PostTask(
      FROM_HERE,
      base::BindOnce(std::move(presenter_.answers.back()), decision));
  presenter_.answers.pop_back();
  auto reply = client->Read();
  ASSERT_TRUE(reply);
  EXPECT_TRUE(reply->FindDict("result")->FindString("clientId"));
  EXPECT_EQ(store_.size(), 1u);
}

TEST_F(KoanAgentGatewayTest, AnAnswerAfterTheGatewayIsGoneDoesNothing) {
  Key key;
  auto client = Connect();
  client->Send(PairFrame(key));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !presenter_.answers.empty(); }));
  auto late = std::move(presenter_.answers.back());
  presenter_.answers.pop_back();
  gateway_->Stop();
  ApprovalDecision decision;
  decision.approved = true;
  std::move(late).Run(decision);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(store_.size(), 0u);
}

TEST_F(KoanAgentGatewayTest,
       AConnectionTheKernelRefusesNeverReachesTheGateway) {
  // A second gateway whose listener lets a different user through: this
  // process is refused by the kernel's check, which accepts the next connection
  // by itself.
  GatewayOptions options;
  options.socket_path = dir_.GetPath().Append("other").Append("agent.sock");
  options.uid = getuid();
  options.kernel_uid_for_testing = getuid() + 1;
  Gateway refusing(options, resolver_, &store_, &presenter_, &ticks_, &wall_);
  ASSERT_EQ(StartAndWait(refusing), Gateway::StartResult::kOk);
  for (int i = 0; i < 3; ++i) {
    TestClient client(options.socket_path);
    ASSERT_TRUE(client.Connect());
    client.Send(R"({"id":1,"method":"Koan.status"})");
    EXPECT_FALSE(client.Read()) << i;  // closed, nothing said
  }
  EXPECT_EQ(refusing.connection_count(), 0u);
}

TEST_F(KoanAgentGatewayTest, APeerThatLeavesInTheMiddleOfAFrameFreesItsSlot) {
  for (const size_t sent :
       {size_t{2}, size_t{6}}) {  // half a header, half a frame
    {
      auto client = LiveConnection();
      const std::vector<uint8_t> frame = *EncodeFrame(base::as_byte_span(
          std::string(R"({"id":2,"method":"Koan.status"})")));
      client->SendBytes(base::span(frame).first(sent));
    }  // gone
    EXPECT_TRUE(WaitForConnections(0)) << sent;
  }
}

TEST_F(KoanAgentGatewayTest, APeerThatLeavesWithAPromptOpenFreesThePromptSlot) {
  Key key;
  {
    auto client = Connect();
    client->Send(PairFrame(key));
    ASSERT_TRUE(
        base::test::RunUntil([&] { return !presenter_.requests.empty(); }));
  }
  ASSERT_TRUE(WaitForConnections(0));
  EXPECT_EQ(presenter_.cancelled.size(), 1u);
}

}  // namespace koan::agent
