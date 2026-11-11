// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/files/scoped_file.h"
#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/cli/koan_cli_admission.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_io.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_cli_router.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::cli {
namespace {

Command Parse(std::vector<std::string> args) {
  ParseResult result = ParseArgs(args);
  EXPECT_TRUE(result.command) << result.error;
  return result.command.value_or(Command());
}

std::string ErrorOf(std::vector<std::string> args) {
  ParseResult result = ParseArgs(args);
  EXPECT_FALSE(result.command);
  return result.error;
}

base::DictValue Dict(std::string_view json) {
  return std::move(*base::JSONReader::Read(json, base::JSON_PARSE_RFC))
      .TakeDict();
}

// --- Arguments ---------------------------------------------------------------

TEST(KoanCliArgsTest, CommandsAndTheirArguments) {
  EXPECT_EQ(Parse({}).kind, CommandKind::kHelp);
  EXPECT_EQ(Parse({"status", "--json"}).kind, CommandKind::kStatus);
  EXPECT_TRUE(Parse({"status", "--json"}).json);

  Command open = Parse(
      {"open", "https://a.test/", "--task", "research", "--saved=GitHub bot"});
  EXPECT_EQ(open.kind, CommandKind::kOpen);
  EXPECT_EQ(open.url, "https://a.test/");
  EXPECT_EQ(open.task, "research");
  EXPECT_EQ(open.saved_container, "GitHub bot");
  EXPECT_EQ(Parse({"open"}).url, "about:blank");

  Command shot = Parse({"screenshot", "abcd", "-o", "x.png"});
  EXPECT_EQ(shot.tab, "abcd");
  EXPECT_EQ(shot.output, "x.png");
  EXPECT_EQ(Parse({"take-over"}).tab, "");
  EXPECT_EQ(Parse({"hand-back", "abcd"}).kind, CommandKind::kRelease);

  Command pair =
      Parse({"pair", "--label", "Claude Code", "--capabilities", "read,drive"});
  EXPECT_EQ(pair.label, "Claude Code");
  EXPECT_EQ(pair.capabilities, (std::vector<std::string>{"read", "drive"}));
}

TEST(KoanCliArgsTest, Actions) {
  Command click = Parse({"act", "abcd", "click", "@12"});
  EXPECT_EQ(click.act, ActKind::kClick);
  EXPECT_EQ(click.ref, "@12");
  Command fill = Parse({"act", "abcd", "fill", "@3", "-hello"});
  EXPECT_EQ(fill.act, ActKind::kFill);
  EXPECT_EQ(fill.text, "-hello");
  EXPECT_EQ(Parse({"act", "abcd", "scroll", "-400"}).scroll_by, -400);
  EXPECT_EQ(Parse({"act", "abcd", "press", "Enter"}).text, "Enter");
  EXPECT_EQ(Parse({"act", "abcd", "navigate", "https://b.test/"}).act,
            ActKind::kNavigate);

  EXPECT_FALSE(ErrorOf({"act", "abcd", "click", "12"}).empty());
  EXPECT_FALSE(ErrorOf({"act", "abcd", "scroll", "0"}).empty());
  EXPECT_FALSE(ErrorOf({"act", "abcd", "wiggle"}).empty());
}

TEST(KoanCliArgsTest, Refusals) {
  EXPECT_NE(ErrorOf({"frobnicate"}).find("unknown command"), std::string::npos);
  EXPECT_NE(ErrorOf({"status", "--bogus"}).find("unknown option"),
            std::string::npos);
  EXPECT_FALSE(ErrorOf({"open", "--task"}).empty());
  EXPECT_FALSE(ErrorOf({"open", "--task", std::string(65, 'x')}).empty());
  EXPECT_FALSE(ErrorOf({"pair", "--capabilities", "root"}).empty());
  EXPECT_FALSE(ErrorOf({"snapshot"}).empty());
  EXPECT_FALSE(ErrorOf({"status", "extra"}).empty());
  // Only koan itself starts a holder.
  EXPECT_FALSE(ErrorOf({"__hold"}).empty());
  EXPECT_EQ(Parse({"__hold", "--ready-fd", "3"}).ready_fd, 3);
}

TEST(KoanCliArgsTest, AgentBrowserTakesTheRestUntouched) {
  Command command =
      Parse({"--user-data-dir", "/p", "agent-browser", "--session", "work",
             "open", "https://a.test", "--json"});
  EXPECT_EQ(command.kind, CommandKind::kAgentBrowser);
  EXPECT_EQ(command.user_data_dir, "/p");
  // Its own options stay its own, --json included.
  EXPECT_EQ(command.passthrough,
            (std::vector<std::string>{"--session", "work", "open",
                                      "https://a.test", "--json"}));
  EXPECT_FALSE(command.json);
  EXPECT_TRUE(Parse({"agent-browser"}).passthrough.empty());

  EXPECT_EQ(Parse({"relay", "env"}).kind, CommandKind::kRelayEnv);
  EXPECT_EQ(Parse({"relay", "env"}).session, "default");
  Command stop = Parse({"relay", "stop", "--session", "work"});
  EXPECT_EQ(stop.kind, CommandKind::kRelayStop);
  EXPECT_EQ(stop.session, "work");
  EXPECT_FALSE(ErrorOf({"relay"}).empty());
  EXPECT_FALSE(ErrorOf({"relay", "start"}).empty());
}

TEST(KoanCliArgsTest, RefsAndTabPrefixes) {
  EXPECT_EQ(ParseRef("@42"), 42);
  EXPECT_FALSE(ParseRef("42"));
  EXPECT_FALSE(ParseRef("@0"));
  EXPECT_FALSE(ParseRef("@x"));

  const std::vector<std::string> ids = {"ABCD1234", "ABCE5678", "FFFF0000"};
  EXPECT_EQ(ResolveTab("ABCD1234", ids), "ABCD1234");
  EXPECT_EQ(ResolveTab("ABCD", ids), "ABCD1234");
  EXPECT_EQ(ResolveTab("FFFF", ids), "FFFF0000");
  // Ambiguous, too short, or unknown.
  EXPECT_FALSE(ResolveTab("ABC", ids));
  EXPECT_FALSE(ResolveTab("ABCX", ids));
  EXPECT_FALSE(ResolveTab("AB", {"AB12"}));
}

// --- Framing -----------------------------------------------------------------

class KoanCliFramingTest : public testing::Test {
 protected:
  void SetUp() override {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    a_ = std::make_unique<FrameChannel>(base::ScopedFD(fds[0]),
                                        agent::kMaxPayload);
    b_ = std::make_unique<FrameChannel>(base::ScopedFD(fds[1]), 1024);
  }

  std::unique_ptr<FrameChannel> a_;
  std::unique_ptr<FrameChannel> b_;
};

TEST_F(KoanCliFramingTest, FramesGoAcrossWhole) {
  ASSERT_TRUE(a_->Write("{\"id\":1}"));
  ASSERT_TRUE(a_->Write("{\"id\":2}"));
  EXPECT_EQ(b_->Read(base::Seconds(5)), "{\"id\":1}");
  EXPECT_EQ(b_->Read(base::Seconds(5)), "{\"id\":2}");
  // Nothing more: a timeout, not a close.
  EXPECT_FALSE(b_->Read(base::Milliseconds(10)));
  EXPECT_FALSE(b_->closed());
}

TEST_F(KoanCliFramingTest, AFrameInPiecesAndAnOversizedOne) {
  // A header, then the payload in two writes.
  const std::string payload = "{\"method\":\"Koan.userNeeded\"}";
  const uint8_t header[4] = {0, 0, 0, static_cast<uint8_t>(payload.size())};
  ASSERT_EQ(write(a_->fd(), header, 4), 4);
  ASSERT_EQ(write(a_->fd(), payload.data(), 5), 5);
  EXPECT_FALSE(b_->Read(base::Milliseconds(10)));
  const base::span<const char> rest = base::span(payload).subspan(5u);
  ASSERT_EQ(write(a_->fd(), rest.data(), rest.size()),
            static_cast<ssize_t>(rest.size()));
  EXPECT_EQ(b_->Read(base::Seconds(5)), payload);

  // More than the reader allows: it gives up on the connection.
  ASSERT_TRUE(a_->Write(std::string(2048, 'x')));
  EXPECT_FALSE(b_->Read(base::Seconds(5)));
  EXPECT_TRUE(b_->closed());
  // And a writer never sends what it may not.
  EXPECT_FALSE(b_->Write(std::string(2048, 'x')));
}

TEST_F(KoanCliFramingTest, TheOtherEndClosing) {
  a_.reset();
  EXPECT_FALSE(b_->Read(base::Seconds(5)));
  EXPECT_TRUE(b_->closed());
}

// --- Protocol ----------------------------------------------------------------

TEST(KoanCliProtocolTest, Discovery) {
  std::optional<Discovery> discovery = ParseDiscovery(
      R"({"socket":"/home/example/Library/x/agent.sock","pid":42,"version":"1"})");
  ASSERT_TRUE(discovery);
  EXPECT_EQ(discovery->socket, "/home/example/Library/x/agent.sock");
  EXPECT_EQ(discovery->pid, 42);
  EXPECT_FALSE(ParseDiscovery(R"({"socket":"relative.sock","pid":42})"));
  EXPECT_FALSE(ParseDiscovery(R"({"socket":"/a.sock","pid":0})"));
  EXPECT_FALSE(ParseDiscovery("not json"));
}

TEST(KoanCliProtocolTest, ClientsAndChoosingOne) {
  const std::string id(32, 'a');
  std::optional<ClientInfo> client =
      ParseClient(SerializeClient({id, "Claude Code"}));
  ASSERT_TRUE(client);
  EXPECT_EQ(client->client_id, id);
  EXPECT_EQ(client->label, "Claude Code");
  // The pin of the pairing goes there and back.
  ClientInfo pinned{id, "Claude Code",
                    agent::PinnedIdentity{{"/Applications/Koan.app/koan", "T1"},
                                          {"/opt/claude", "ANTHROPIC"}}};
  std::optional<ClientInfo> read = ParseClient(SerializeClient(pinned));
  ASSERT_TRUE(read && read->pinned);
  EXPECT_EQ(*read->pinned, *pinned.pinned);
  EXPECT_FALSE(client->pinned);
  // A label never becomes a path: the id must be the browser's.
  EXPECT_FALSE(ParseClient(R"({"clientId":"../../etc"})"));
  EXPECT_FALSE(
      ParseClient(R"({"clientId":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"})"));

  std::string error;
  EXPECT_EQ(ChooseClient({id}, "", &error), id);
  EXPECT_FALSE(ChooseClient({}, "", &error));
  EXPECT_NE(error.find("koan pair"), std::string::npos);
  EXPECT_FALSE(ChooseClient({id, std::string(32, 'b')}, "", &error));
  EXPECT_EQ(
      ChooseClient({id, std::string(32, 'b')}, std::string(32, 'b'), &error),
      std::string(32, 'b'));
}

TEST(KoanCliProtocolTest, MessagesAndErrors) {
  std::optional<Message> answer = ParseMessage(
      R"({"id":3,"error":{"code":-32000,"message":"no","data":"Koan.notGranted"}})");
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->id, 3);
  EXPECT_TRUE(answer->is_error());
  EXPECT_EQ(answer->error_code, "Koan.notGranted");

  std::optional<Message> event = ParseMessage(
      R"({"method":"Koan.userNeeded","params":{"targetId":"T","reason":"print"}})");
  ASSERT_TRUE(event);
  EXPECT_FALSE(event->id);
  EXPECT_EQ(*event->params.FindString("reason"), "print");
  EXPECT_FALSE(ParseMessage("{}"));

  EXPECT_EQ(
      Dict(Request(7, "Koan.openTab", base::DictValue().Set("url", "x"), "S1")),
      Dict(R"({"id":7,"method":"Koan.openTab","params":{"url":"x"},
                     "sessionId":"S1"})"));
}

TEST(KoanCliProtocolTest, SnapshotOutline) {
  const base::DictValue tree = Dict(R"({"nodes":[
    {"nodeId":"1","role":{"value":"RootWebArea"},"name":{"value":"Shop"},
     "childIds":["2","5"],"backendDOMNodeId":1},
    {"nodeId":"2","role":{"value":"generic"},"childIds":["3","4"]},
    {"nodeId":"3","role":{"value":"button"},"name":{"value":"Buy \"now\""},
     "backendDOMNodeId":12},
    {"nodeId":"4","ignored":true,"role":{"value":"none"},"childIds":["6"]},
    {"nodeId":"6","role":{"value":"textbox"},"name":{"value":"Email"},
     "value":{"value":"a@b.test"},"backendDOMNodeId":34},
    {"nodeId":"5","role":{"value":"StaticText"},"name":{"value":"Total"}}
  ]})");
  EXPECT_EQ(SnapshotText(tree, 100),
            "RootWebArea \"Shop\" @1\n"
            "  button \"Buy \\\"now\\\"\" @12\n"
            "  textbox \"Email\" = \"a@b.test\" @34\n"
            "  \"Total\"\n");
  EXPECT_EQ(SnapshotText(tree, 1),
            "RootWebArea \"Shop\" @1\n... (more; the page is long)\n");
  EXPECT_EQ(SnapshotText(base::DictValue(), 10), "");
}

TEST(KoanCliProtocolTest, KeysAndQuads) {
  std::optional<base::DictValue> enter = KeyDownFor("Enter");
  ASSERT_TRUE(enter);
  EXPECT_EQ(*enter->FindString("key"), "Enter");
  EXPECT_EQ(enter->FindInt("windowsVirtualKeyCode"), 13);
  EXPECT_EQ(*KeyDownFor("a")->FindString("text"), "a");
  EXPECT_FALSE(KeyDownFor("Hyper"));
  EXPECT_FALSE(KeyDownFor("\x01"));

  std::optional<std::pair<double, double>> center =
      QuadCenter(Dict(R"({"quads":[[10,20,30,20,30,40,10,40]]})"));
  ASSERT_TRUE(center);
  EXPECT_DOUBLE_EQ(center->first, 20);
  EXPECT_DOUBLE_EQ(center->second, 30);
  EXPECT_FALSE(QuadCenter(Dict(R"({"quads":[]})")));
}

TEST(KoanCliProtocolTest, PathsStayShort) {
  const base::FilePath home("/home/someone-with-a-rather-long-user-name");
  // A unix socket path is at most 103 characters on the Mac.
  EXPECT_LT(HolderSocket(KoanHome(home), std::string(32, 'a')).value().size(),
            104u);
  EXPECT_EQ(DefaultUserDataDir(home, "/tmp/p").value(), "/tmp/p");
}

// --- Who may use the holder --------------------------------------------------

class KoanCliAdmissionTest : public testing::Test {
 protected:
  // `koan` run by `agent` through a shell.
  agent::PeerIdentity Peer(agent::ProcessInfo process,
                           agent::ProcessInfo agent_process,
                           uint32_t uid = 501) {
    agent::PeerIdentity peer;
    peer.uid = uid;
    peer.process = std::move(process);
    peer.ancestors = {{"/bin/zsh", "com.apple.zsh"},
                      std::move(agent_process),
                      {"/sbin/launchd", "com.apple.launchd"}};
    return peer;
  }

  const agent::ProcessInfo koan_{
      "/Applications/Koan.app/Contents/Resources/bin/koan", "ad-hoc:abc"};
  const agent::ProcessInfo claude_{"/opt/claude/bin/claude", "ANTHROPIC"};
};

TEST_F(KoanCliAdmissionTest, OnlyThePairedAgentsKoan) {
  EXPECT_EQ(DecideLocalClient(koan_, claude_, 501, Peer(koan_, claude_)),
            LocalClientVerdict::kAllowed);
  // Another program of the same user: a script, a copy of koan elsewhere,
  // koan with another signature.
  EXPECT_EQ(DecideLocalClient(koan_, claude_, 501,
                              Peer({"/usr/bin/python3", ""}, claude_)),
            LocalClientVerdict::kOtherProgram);
  EXPECT_EQ(DecideLocalClient(koan_, claude_, 501,
                              Peer({"/tmp/koan", "ad-hoc:abc"}, claude_)),
            LocalClientVerdict::kOtherProgram);
  EXPECT_EQ(DecideLocalClient(koan_, claude_, 501,
                              Peer({koan_.path, "ad-hoc:evil"}, claude_)),
            LocalClientVerdict::kOtherProgram);
  // The right koan, run by something other than the paired agent.
  EXPECT_EQ(DecideLocalClient(koan_, claude_, 501,
                              Peer(koan_, {"/Applications/Other.app/o", "X"})),
            LocalClientVerdict::kAgentMissing);
  // Another user, or nobody the OS could name.
  EXPECT_EQ(DecideLocalClient(koan_, claude_, 501, Peer(koan_, claude_, 502)),
            LocalClientVerdict::kOtherUser);
  EXPECT_EQ(DecideLocalClient(koan_, claude_, 501, std::nullopt),
            LocalClientVerdict::kUnresolved);
  EXPECT_EQ(DecideLocalClient(koan_, claude_, 501, Peer({"", ""}, claude_)),
            LocalClientVerdict::kUnresolved);
  // A holder that could not name itself serves nobody.
  EXPECT_EQ(DecideLocalClient({"", ""}, claude_, 501, Peer(koan_, claude_)),
            LocalClientVerdict::kUnresolved);
}

TEST_F(KoanCliAdmissionTest, AnUnknownChainAtPairingStaysUnknown) {
  // As in the browser: a pairing with no agent in its chain does not let a
  // koan that an agent runs in.
  agent::PeerIdentity orphan;
  orphan.uid = 501;
  orphan.process = koan_;
  // Only a shell: nothing that counts as an agent.
  orphan.ancestors = {{"/bin/zsh", "com.apple.zsh"}};
  EXPECT_EQ(DecideLocalClient(koan_, {}, 501, orphan),
            LocalClientVerdict::kAllowed);
  EXPECT_EQ(DecideLocalClient(koan_, {}, 501, Peer(koan_, claude_)),
            LocalClientVerdict::kAgentMissing);
  EXPECT_EQ(VerdictName(LocalClientVerdict::kAgentMissing), "agent-missing");
}

// --- The holder's routing ----------------------------------------------------

TEST(KoanCliRouterTest, AnswersGoBackToWhoAskedWithTheirIds) {
  Router router;
  Router::Outgoing a =
      router.FromCommand(1, R"({"id":1,"method":"Koan.listTabs","params":{}})");
  Router::Outgoing b =
      router.FromCommand(2, R"({"id":1,"method":"Koan.listTabs","params":{}})");
  ASSERT_TRUE(a.to_browser);
  ASSERT_TRUE(b.to_browser);
  const int a_id = *Dict(*a.to_browser).FindInt("id");
  const int b_id = *Dict(*b.to_browser).FindInt("id");
  EXPECT_NE(a_id, b_id);

  std::vector<Router::Delivery> out = router.FromBrowser(
      R"({"id":)" + std::to_string(b_id) + R"(,"result":{"tabs":[]}})");
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].connection, 2u);
  EXPECT_EQ(Dict(out[0].json).FindInt("id"), 1);
  // An answer for a command that went away goes nowhere.
  router.RemoveConnection(1);
  EXPECT_TRUE(
      router
          .FromBrowser(R"({"id":)" + std::to_string(a_id) + R"(,"result":{}})")
          .empty());
  EXPECT_FALSE(router.has_pending());
}

TEST(KoanCliRouterTest, OneSessionPerTab) {
  Router router;
  Router::Outgoing first =
      router.FromCommand(1, R"({"id":5,"method":"Target.attachToTarget",
             "params":{"targetId":"T1","flatten":true}})");
  ASSERT_TRUE(first.to_browser);
  router.FromBrowser(R"({"id":)" +
                     std::to_string(*Dict(*first.to_browser).FindInt("id")) +
                     R"(,"result":{"sessionId":"S1"}})");
  // The next command for that tab gets the same session at once.
  Router::Outgoing second =
      router.FromCommand(2, R"({"id":9,"method":"Target.attachToTarget",
             "params":{"targetId":"T1","flatten":true}})");
  EXPECT_FALSE(second.to_browser);
  ASSERT_TRUE(second.to_command);
  EXPECT_EQ(Dict(*second.to_command), Dict(R"({"id":9,
            "result":{"sessionId":"S1"}})"));
  // Until the tab goes.
  router.FromBrowser(
      R"({"method":"Target.detachedFromTarget","params":{"sessionId":"S1"}})");
  EXPECT_TRUE(router
                  .FromCommand(2, R"({"id":10,"method":"Target.attachToTarget",
                       "params":{"targetId":"T1","flatten":true}})")
                  .to_browser);
}

TEST(KoanCliRouterTest, TasksEventsAndWatchers) {
  Router router;
  auto answer = [&](const Router::Outgoing& out, std::string result) {
    router.FromBrowser(R"({"id":)" +
                       std::to_string(*Dict(*out.to_browser).FindInt("id")) +
                       R"(,"result":)" + result + "}");
  };
  answer(router.FromCommand(1, R"({"id":1,"method":"Koan.openTab",
                                   "params":{"url":"about:blank"}})"),
         R"({"targetId":"T1"})");
  answer(router.FromCommand(1, R"({"id":2,"method":"Koan.openTab",
                                   "params":{"savedContainer":"bot"}})"),
         R"({"targetId":"T2"})");
  // A refused open is no task.
  router.FromBrowser(
      R"({"id":)" +
      std::to_string(
          *Dict(*router
                     .FromCommand(1, R"({"id":3,"method":"Koan.openTab",
                                   "params":{"task":"t"}})")
                     .to_browser)
               .FindInt("id")) +
      R"(,"error":{"code":-32000,"message":"no","data":"Koan.notGranted"}})");
  EXPECT_EQ(router.tasks(), (std::set<std::string>{"Throwaway", "bot"}));
  answer(router.FromCommand(1, R"({"id":4,"method":"Koan.endTask",
                                   "params":{"task":"bot"}})"),
         "{}");
  EXPECT_EQ(router.tasks(), (std::set<std::string>{"Throwaway"}));

  router.AddWatcher(7);
  EXPECT_EQ(router
                .FromBrowser(R"({"method":"Koan.userNeeded",
                                 "params":{"targetId":"T1","reason":"print"}})")
                .size(),
            1u);
  // A page's own events are not the watcher's.
  EXPECT_TRUE(router
                  .FromBrowser(R"({"method":"Network.requestWillBeSent",
                                   "params":{},"sessionId":"S1"})")
                  .empty());
  ASSERT_EQ(router.events().size(), 1u);
  for (size_t i = 0; i < Router::kMaxEvents + 5; ++i) {
    router.FromBrowser(R"({"method":"Koan.paused","params":{}})");
  }
  EXPECT_EQ(router.events().size(), Router::kMaxEvents);
}

}  // namespace
}  // namespace koan::cli
