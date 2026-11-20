// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/cli/koan_cli_actions.h"
#include "koan/cli/koan_cli_admission.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_client.h"
#include "koan/cli/koan_cli_io.h"
#include "koan/cli/koan_cli_keepalive.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_cli_router.h"
#include "koan/cli/koan_cli_skill.h"
#include "koan/cli/koan_cli_snapshot.h"
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
  EXPECT_EQ(Parse({"watch"}).tab, "");
  EXPECT_EQ(Parse({"watch", "abcd"}).tab, "abcd");
  EXPECT_EQ(ErrorOf({"watch", "abcd", "efgh"}), "usage: koan watch [tab]");
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

  // A dropdown's option, by label or value.
  Command select = Parse({"act", "abcd", "select", "@7", "Pro plan"});
  EXPECT_EQ(select.act, ActKind::kSelect);
  EXPECT_EQ(select.ref, "@7");
  EXPECT_EQ(select.text, "Pro plan");
  EXPECT_FALSE(ErrorOf({"act", "abcd", "select", "@7"}).empty());
  EXPECT_FALSE(ErrorOf({"act", "abcd", "select", "@7", ""}).empty());
  EXPECT_FALSE(ErrorOf({"act", "abcd", "select", "7", "Pro"}).empty());

  // --full is the snapshot's; an act's text may be "--full".
  EXPECT_TRUE(Parse({"snapshot", "abcd", "--full"}).full);
  EXPECT_FALSE(Parse({"snapshot", "abcd"}).full);
  EXPECT_NE(ErrorOf({"tabs", "--full"}).find("--full"), std::string::npos);
  EXPECT_EQ(Parse({"act", "abcd", "fill", "@3", "--full"}).text, "--full");
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

TEST(KoanCliArgsTest, SettleChooseAndShell) {
  Command click = Parse({"act", "abcd", "click", "@12", "--snapshot"});
  EXPECT_TRUE(click.snapshot_after);
  EXPECT_EQ(click.timeout_seconds, 15);
  EXPECT_FALSE(Parse({"act", "abcd", "click", "@12"}).snapshot_after);
  Command navigate = Parse({"act", "abcd", "navigate", "https://b.test/",
                            "--timeout", "40", "--snapshot"});
  EXPECT_EQ(navigate.text, "https://b.test/");
  EXPECT_EQ(navigate.timeout_seconds, 40);
  EXPECT_TRUE(navigate.snapshot_after);
  EXPECT_TRUE(
      Parse({"act", "--snapshot", "abcd", "press", "Enter"}).snapshot_after);
  EXPECT_EQ(
      Parse({"act", "abcd", "press", "Tab", "--timeout=5"}).timeout_seconds, 5);
  // Where the act's text goes, it is the text.
  Command fill = Parse({"act", "abcd", "fill", "@3", "--snapshot"});
  EXPECT_EQ(fill.text, "--snapshot");
  EXPECT_FALSE(fill.snapshot_after);
  EXPECT_TRUE(Parse({"act", "abcd", "fill", "@3", "--snapshot", "--snapshot"})
                  .snapshot_after);

  Command choose =
      Parse({"act", "abcd", "choose", "@7", "United Kingdom", "--snapshot"});
  EXPECT_EQ(choose.act, ActKind::kChoose);
  EXPECT_EQ(choose.ref, "@7");
  EXPECT_EQ(choose.text, "United Kingdom");
  EXPECT_TRUE(choose.snapshot_after);
  EXPECT_FALSE(ErrorOf({"act", "abcd", "choose", "@7"}).empty());
  EXPECT_FALSE(ErrorOf({"act", "abcd", "choose", "7", "UK"}).empty());

  EXPECT_FALSE(
      ErrorOf({"act", "abcd", "click", "@1", "--timeout", "0"}).empty());
  EXPECT_FALSE(
      ErrorOf({"act", "abcd", "click", "@1", "--timeout", "soon"}).empty());
  EXPECT_NE(ErrorOf({"snapshot", "abcd", "--snapshot"}).find("koan act's"),
            std::string::npos);

  EXPECT_EQ(Parse({"shell"}).kind, CommandKind::kShell);
  EXPECT_FALSE(Parse({"shell"}).stop_on_error);
  EXPECT_TRUE(Parse({"shell", "--stop-on-error"}).stop_on_error);
  EXPECT_FALSE(ErrorOf({"shell", "now"}).empty());
  EXPECT_NE(ErrorOf({"tabs", "--stop-on-error"}).find("koan shell's"),
            std::string::npos);
}

// koan wait <tab> --resumed [--timeout <duration>] (design 9.2).
TEST(KoanCliArgsTest, WaitResumed) {
  Command wait = Parse({"wait", "abcd", "--resumed"});
  EXPECT_EQ(wait.kind, CommandKind::kWait);
  EXPECT_EQ(wait.tab, "abcd");
  EXPECT_TRUE(wait.resumed);
  EXPECT_EQ(wait.wait_seconds, 600);
  EXPECT_EQ(
      Parse({"wait", "--resumed", "abcd", "--timeout", "90s"}).wait_seconds,
      90);
  EXPECT_EQ(Parse({"wait", "abcd", "--resumed", "--timeout=2m"}).wait_seconds,
            120);
  EXPECT_EQ(
      Parse({"wait", "abcd", "--resumed", "--timeout", "1h"}).wait_seconds,
      3600);
  EXPECT_EQ(
      Parse({"wait", "abcd", "--resumed", "--timeout", "45"}).wait_seconds, 45);
  // act's --timeout is still seconds, up to 600.
  EXPECT_EQ(Parse({"act", "abcd", "press", "Tab", "--timeout", "600"})
                .timeout_seconds,
            600);
  EXPECT_FALSE(
      ErrorOf({"act", "abcd", "press", "Tab", "--timeout", "2m"}).empty());

  for (const char* bad :
       {"0", "0s", "61m", "2h", "3601", "-5", "soon", "m", "", "1.5m"}) {
    EXPECT_NE(ErrorOf({"wait", "abcd", "--resumed", "--timeout", bad})
                  .find("--timeout"),
              std::string::npos)
        << bad;
  }
  // What it waits for, and on which tab, must be said.
  EXPECT_NE(ErrorOf({"wait", "abcd"}).find("--resumed"), std::string::npos);
  EXPECT_FALSE(ErrorOf({"wait", "--resumed"}).empty());
  EXPECT_FALSE(ErrorOf({"wait", "a", "b", "--resumed"}).empty());
  EXPECT_NE(ErrorOf({"snapshot", "abcd", "--resumed"}).find("koan wait's"),
            std::string::npos);
  EXPECT_NE(ErrorOf({"tabs", "--timeout", "5"}).find("koan act's"),
            std::string::npos);
  EXPECT_TRUE(HelpText("wait"));

  EXPECT_EQ(ParseWaitDuration("30s"), 30);
  EXPECT_EQ(ParseWaitDuration("60m"), 3600);
  EXPECT_FALSE(ParseWaitDuration("10x"));
}

TEST(KoanCliShellTest, LinesAsAShellSplitsThem) {
  ShellLine plain =
      ParseShellLine("koan act ab12 fill @3 \"Ada Lovelace\" --snapshot\n");
  EXPECT_FALSE(plain.json);
  EXPECT_TRUE(plain.error.empty());
  EXPECT_EQ(plain.args,
            (std::vector<std::string>{"act", "ab12", "fill", "@3",
                                      "Ada Lovelace", "--snapshot"}));
  EXPECT_EQ(ParseShellLine("act t fill @3 'it''s'").args.back(), "its");
  EXPECT_EQ(ParseShellLine(R"(act t fill @3 'a "b" \c')").args.back(),
            R"(a "b" \c)");
  EXPECT_EQ(
      ParseShellLine(R"(act t fill @3 "say \"hi\" \\ $x \n")").args.back(),
      R"(say "hi" \ $x \n)");
  EXPECT_EQ(ParseShellLine(R"(act t fill @3 a\ b)").args.back(), "a b");
  // An empty argument is one.
  EXPECT_EQ(ParseShellLine(R"(act t fill @3 "")").args,
            (std::vector<std::string>{"act", "t", "fill", "@3", ""}));
  EXPECT_TRUE(ParseShellLine("   \n").empty);
  EXPECT_TRUE(ParseShellLine("  # the next step").empty);
  EXPECT_EQ(ParseShellLine("act t fill @3 \"open").error,
            "unclosed double quote");
  EXPECT_EQ(ParseShellLine("koan").error, "no command");
}

TEST(KoanCliShellTest, JsonLines) {
  ShellLine array = ParseShellLine(R"( ["act","t","click","@4"] )");
  EXPECT_TRUE(array.json);
  EXPECT_EQ(array.id, "null");
  EXPECT_EQ(array.args, (std::vector<std::string>{"act", "t", "click", "@4"}));
  ShellLine object = ParseShellLine(R"({"id":7,"args":["koan","tabs"]})");
  EXPECT_TRUE(object.json);
  EXPECT_EQ(object.id, "7");
  EXPECT_EQ(object.args, std::vector<std::string>{"tabs"});
  EXPECT_EQ(ParseShellLine(R"({"id":"step-2","args":["tabs"]})").id,
            "\"step-2\"");
  // Every refusal is a JSON line too, answered as one.
  for (const char* bad : {R"({"id":1})", R"(["act",1])", "[]", "{nope"}) {
    ShellLine line = ParseShellLine(bad);
    EXPECT_TRUE(line.json) << bad;
    EXPECT_FALSE(line.error.empty()) << bad;
  }
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

// The holder (or the browser) turns a command away with a refusal and closes:
// the command gets that as its answer, not "no answer".
TEST_F(KoanCliFramingTest, ARefusalIsTheAnswer) {
  ASSERT_TRUE(b_->Write(Refusal("Hold.refused", "not the paired agent")));
  b_.reset();
  Link link(std::move(a_));
  std::optional<Message> answer =
      link.Call("Koan.openTab", base::DictValue(), {}, base::Seconds(5));
  ASSERT_TRUE(answer);
  EXPECT_FALSE(answer->id);
  EXPECT_EQ(answer->error_code, "Hold.refused");
  EXPECT_EQ(Describe(answer), "Hold.refused: not the paired agent");
}

// A wait on a tab keeps it alive (Koan.keepAlive) and ends that when it is
// over; a keep-alive the browser refused is not ended.
TEST_F(KoanCliFramingTest, AWaitKeepsTheTabAlive) {
  ASSERT_TRUE(b_->Write(R"({"id":1,"result":{"holdId":"7.1","seconds":30}})"));
  ASSERT_TRUE(b_->Write(R"({"id":2,"result":{}})"));
  ASSERT_TRUE(b_->Write(
      R"({"id":3,"error":{"code":-32000,"message":"no","data":"Koan.lowPower"}})"));
  Link link(std::move(a_));
  {
    TabKeepAlive keep_alive(link, "TAB1");
    EXPECT_TRUE(keep_alive.held());
    EXPECT_GT(keep_alive.next_renewal(), base::TimeTicks::Now());
    // Not due yet: nothing is sent.
    keep_alive.RenewIfDue();
  }
  {
    TabKeepAlive refused(link, "TAB1");
    EXPECT_FALSE(refused.held());
  }
  auto request = [&] {
    std::optional<std::string> frame = b_->Read(base::Seconds(5));
    return frame ? Dict(*frame) : base::DictValue();
  };
  const base::DictValue asked = request();
  EXPECT_EQ(*asked.FindString("method"), "Koan.keepAlive");
  EXPECT_EQ(*asked.FindStringByDottedPath("params.targetId"), "TAB1");
  EXPECT_EQ(asked.FindIntByDottedPath("params.seconds"), 30);
  const base::DictValue ended = request();
  EXPECT_EQ(*ended.FindStringByDottedPath("params.holdId"), "7.1");
  EXPECT_EQ(ended.FindIntByDottedPath("params.seconds"), 0);
  EXPECT_FALSE(ended.FindStringByDottedPath("params.targetId"));
  const base::DictValue again = request();
  EXPECT_EQ(*again.FindStringByDottedPath("params.targetId"), "TAB1");
  // The refused one sent no end.
  EXPECT_FALSE(b_->Read(base::Milliseconds(50)));
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

  // The profile and the time of the pairing go there and back; a record
  // from before them has neither.
  ClientInfo dated{id, "Claude Code", std::nullopt,
                   base::FilePath("/Users/me/Library/Application Support/Koan"),
                   base::Time::FromMillisecondsSinceUnixEpoch(1763500000000)};
  read = ParseClient(SerializeClient(dated));
  ASSERT_TRUE(read);
  EXPECT_EQ(read->user_data_dir, dated.user_data_dir);
  EXPECT_EQ(read->paired_at, dated.paired_at);
  EXPECT_TRUE(client->user_data_dir.empty());
  EXPECT_TRUE(client->paired_at.is_null());
}

// Plain `koan pair` asks for everything (the user's rule: agents use the
// browser as the user does), so that take-over works without knowing about
// --capabilities; --capabilities narrows it.
TEST(KoanCliProtocolTest, PairAsksForEveryCapabilityByDefault) {
  EXPECT_EQ(PairCapabilities({}),
            (std::vector<std::string>{"space", "observe", "read", "drive",
                                      "keep-awake"}));
  EXPECT_EQ(PairCapabilities({"space"}), std::vector<std::string>{"space"});
  // Every default is a name the browser takes.
  EXPECT_TRUE(agent::ParseCapabilities(PairCapabilities({}))->drive);
  EXPECT_EQ(Parse({"pair", "--label", "x"}).capabilities.size(), 0u);
  EXPECT_EQ(Parse({"pair", "--capabilities", "space"}).capabilities,
            std::vector<std::string>{"space"});
}

TEST(KoanCliProtocolTest, ChoosingAClient) {
  const base::FilePath koan("/Users/me/Library/Application Support/Koan");
  const base::FilePath test("/tmp/profile");
  const agent::ProcessInfo claude{"/opt/claude", "ANTHROPIC"};
  const agent::ProcessInfo codex{"/opt/codex", "OPENAI"};
  auto client = [](char c, base::FilePath dir, int minutes,
                   std::optional<agent::ProcessInfo> agent = std::nullopt) {
    ClientInfo info{std::string(32, c), "x", std::nullopt, dir,
                    base::Time::UnixEpoch() + base::Minutes(minutes)};
    if (agent) {
      info.pinned = agent::PinnedIdentity{{"/koan", "T"}, *agent};
    }
    return info;
  };
  std::string error;
  EXPECT_FALSE(ChooseClient({}, "", koan, std::nullopt, &error));
  EXPECT_NE(error.find("koan pair"), std::string::npos);

  // Several pairings: the newest is used, so a new `koan pair` needs no
  // --client (the 2026-11-19 agent: "several paired clients ...").
  const std::vector<ClientInfo> two = {client('a', koan, 1),
                                       client('b', koan, 2)};
  EXPECT_EQ(ChooseClient(two, "", koan, std::nullopt, &error),
            std::string(32, 'b'));
  EXPECT_EQ(ChooseClient(two, std::string(32, 'a'), koan, std::nullopt, &error),
            std::string(32, 'a'));
  EXPECT_FALSE(
      ChooseClient(two, std::string(32, 'c'), koan, std::nullopt, &error));

  // Only pairings with the Koan dialled, or ones that do not say.
  const std::vector<ClientInfo> profiles = {client('a', koan, 1),
                                            client('b', test, 2)};
  EXPECT_EQ(ChooseClient(profiles, "", koan, std::nullopt, &error),
            std::string(32, 'a'));
  EXPECT_EQ(ChooseClient(profiles, "", test, std::nullopt, &error),
            std::string(32, 'b'));
  EXPECT_FALSE(
      ChooseClient({client('b', test, 2)}, "", koan, std::nullopt, &error));
  EXPECT_NE(error.find(koan.value()), std::string::npos);
  EXPECT_EQ(ChooseClient({client('a', base::FilePath(), 1)}, "", koan,
                         std::nullopt, &error),
            std::string(32, 'a'));

  // Two agents: each gets its own pairing, newest first; an agent with none
  // gets the newest.
  const std::vector<ClientInfo> agents = {client('a', koan, 1, claude),
                                          client('b', koan, 2, codex),
                                          client('c', koan, 3, claude)};
  EXPECT_EQ(ChooseClient(agents, "", koan, claude, &error),
            std::string(32, 'c'));
  EXPECT_EQ(ChooseClient(agents, "", koan, codex, &error),
            std::string(32, 'b'));
  EXPECT_EQ(
      ChooseClient(agents, "", koan,
                   agent::ProcessInfo{"/opt/cursor", "ANYSPHERE"}, &error),
      std::string(32, 'c'));
}

// A pairing the browser refused as unknown is removed only if it paired
// with that browser: one of another profile's may still be good there.
TEST(KoanCliProtocolTest, WhichStalePairingsAreForgotten) {
  const base::FilePath koan("/Users/me/Library/Application Support/Koan");
  const base::FilePath test("/tmp/profile");
  ClientInfo record{std::string(32, 'a'), "x", std::nullopt, koan,
                    base::Time()};
  EXPECT_TRUE(ShouldForget(record, koan, koan));
  EXPECT_FALSE(ShouldForget(record, test, koan));
  record.user_data_dir = test;
  EXPECT_TRUE(ShouldForget(record, test, koan));
  EXPECT_FALSE(ShouldForget(record, koan, koan));
  // A record from before pairings said which profile: only Koan's own.
  record.user_data_dir = base::FilePath();
  EXPECT_TRUE(ShouldForget(record, koan, koan));
  EXPECT_FALSE(ShouldForget(record, test, koan));
}

TEST(KoanCliClientTest, PairedClientsAndForgettingOne) {
  base::ScopedTempDir home;
  ASSERT_TRUE(home.CreateUniqueTempDir());
  const base::FilePath koan_home = home.GetPath().Append(".koan");
  const std::string a(32, 'a'), b(32, 'b');
  for (const std::string& id : {a, b}) {
    ASSERT_TRUE(base::CreateDirectory(ClientDir(koan_home, id)));
    ASSERT_TRUE(base::WriteFile(ClientDir(koan_home, id).Append("key"), "k"));
  }
  // `a` is from before pairings kept their time: it gets its file's.
  ASSERT_TRUE(base::WriteFile(ClientDir(koan_home, a).Append("client.json"),
                              SerializeClient({a, "old"})));
  ClientInfo dated{b, "new", std::nullopt, base::FilePath("/p"),
                   base::Time::Now() + base::Days(1)};
  ASSERT_TRUE(base::WriteFile(ClientDir(koan_home, b).Append("client.json"),
                              SerializeClient(dated)));
  std::vector<ClientInfo> clients = PairedClients(koan_home);
  ASSERT_EQ(clients.size(), 2u);
  EXPECT_EQ(clients[0].client_id, a);
  EXPECT_FALSE(clients[0].paired_at.is_null());
  // client.json keeps the time in milliseconds.
  EXPECT_EQ(clients[1].paired_at.InMillisecondsSinceUnixEpoch(),
            dated.paired_at.InMillisecondsSinceUnixEpoch());

  EXPECT_TRUE(ForgetClient(koan_home, b));
  EXPECT_FALSE(base::PathExists(ClientDir(koan_home, b)));
  clients = PairedClients(koan_home);
  ASSERT_EQ(clients.size(), 1u);
  EXPECT_EQ(clients[0].client_id, a);
  // Never a path made of something that is not a client id.
  EXPECT_FALSE(ForgetClient(koan_home, "../.."));
  EXPECT_TRUE(base::PathExists(ClientDir(koan_home, a)));
}

// What the guide must tell an agent (the 2026-11-19 agent that failed): a
// plain pair, take-over of the tab in front, what the tabs use, no
// AppleScript or debugging port, and how to run koan from another machine.
TEST(KoanCliSkillTest, TheGuideCoversPairingTakeOverAndSsh) {
  const std::string guide = SkillGuide("");
  for (const char* needed : {
           "koan pair --label",
           "koan take-over",
           "bring that tab to the front",
           "capability drive",
           "logins, extensions",
           "What agents can use",
           "AppleScript",
           "debugging port",
           "ssh",
           "~/Applications/Koan.app/Contents/Resources/bin/koan",
           "/Applications/Koan.app/Contents/Resources/bin/koan",
           "saves the PNG",
           "never read or edit ~/.koan",
           "--snapshot",
           "act <tab> choose @",
           "koan shell",
           "--- exit",
           "HTTP 504",
           "Koan.timeout",
           "ControlMaster=auto",
           // Auto-resume (design 9.2).
           "koan wait <tab> --resumed",
           "not from your old plan",
           "never retry around it",
       }) {
    EXPECT_NE(guide.find(needed), std::string::npos) << needed;
  }
  // Not the old advice: agent tabs never saw the user's logins.
  EXPECT_EQ(guide.find("never see the user's logins"), std::string::npos);
  EXPECT_EQ(guide.find("--capabilities"), std::string::npos);
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
  // A refusal: an error with no id.
  std::optional<Message> refusal = ParseMessage(Refusal("Koan.busy", "retry"));
  ASSERT_TRUE(refusal);
  EXPECT_FALSE(refusal->id);
  EXPECT_EQ(refusal->error_code, "Koan.busy");
  EXPECT_EQ(refusal->error_message, "retry");

  EXPECT_EQ(
      Dict(Request(7, "Koan.openTab", base::DictValue().Set("url", "x"), "S1")),
      Dict(R"({"id":7,"method":"Koan.openTab","params":{"url":"x"},
                     "sessionId":"S1"})"));
}

// --- Snapshots ---------------------------------------------------------------

TEST(KoanCliSnapshotTest, Outline) {
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
  EXPECT_EQ(BuildSnapshot(tree, {}).text,
            "RootWebArea \"Shop\" @1\n"
            "  button \"Buy \\\"now\\\"\" @12\n"
            "  textbox \"Email\" = \"a@b.test\" @34\n"
            "  \"Total\"\n");
  EXPECT_EQ(BuildSnapshot(tree, {.full = true}).text,
            "RootWebArea \"Shop\" @1\n"
            "  button \"Buy \\\"now\\\"\" @12\n"
            "  textbox \"Email\" = \"a@b.test\" @34\n"
            "  StaticText \"Total\"\n");
  EXPECT_EQ(BuildSnapshot(tree, {.max_lines = 1}).text,
            "RootWebArea \"Shop\" @1\n... (more; the page is long)\n");
  EXPECT_EQ(BuildSnapshot(tree, {}).refs, (std::vector<int>{1, 12, 34}));
  EXPECT_EQ(BuildSnapshot(base::DictValue(), {}).text, "");
}

// A sign-up form as Accessibility.getFullAXTree gives it: inline text boxes,
// split text runs, labels next to their controls, a closed dropdown.
constexpr char kFormTree[] = R"({"nodes":[
  {"nodeId":"1","role":{"value":"RootWebArea"},"name":{"value":"Sign up"},
   "childIds":["2"],"backendDOMNodeId":1},
  {"nodeId":"2","role":{"value":"main"},"childIds":["3","4","10","13","20"],
   "backendDOMNodeId":2},
  {"nodeId":"3","role":{"value":"heading"},"name":{"value":"Sign up"},
   "properties":[{"name":"level","value":{"type":"integer","value":1}}],
   "childIds":["31"],"backendDOMNodeId":3},
  {"nodeId":"31","role":{"value":"StaticText"},"name":{"value":"Sign up"},
   "childIds":["32"],"backendDOMNodeId":31},
  {"nodeId":"32","role":{"value":"InlineTextBox"},"name":{"value":"Sign up"}},
  {"nodeId":"4","role":{"value":"paragraph"},
   "childIds":["41","42","43","44","45"],"backendDOMNodeId":4},
  {"nodeId":"41","role":{"value":"StaticText"},"name":{"value":"Fill in "},
   "backendDOMNodeId":41},
  {"nodeId":"42","role":{"value":"generic"},"childIds":["421"],
   "backendDOMNodeId":42},
  {"nodeId":"421","role":{"value":"StaticText"},"name":{"value":"all"},
   "backendDOMNodeId":421},
  {"nodeId":"43","role":{"value":"StaticText"},"name":{"value":" fields"},
   "backendDOMNodeId":43},
  {"nodeId":"44","role":{"value":"LineBreak"},"name":{"value":"\n"},
   "backendDOMNodeId":44},
  {"nodeId":"45","role":{"value":"StaticText"},"name":{"value":"please."},
   "backendDOMNodeId":45},
  {"nodeId":"10","role":{"value":"LabelText"},"childIds":["11","12"],
   "backendDOMNodeId":10},
  {"nodeId":"11","role":{"value":"radio"},"name":{"value":"Email"},
   "properties":[{"name":"focusable","value":{"type":"booleanOrUndefined","value":true}},
                 {"name":"checked","value":{"type":"tristate","value":"true"}}],
   "backendDOMNodeId":11},
  {"nodeId":"12","role":{"value":"StaticText"},"name":{"value":"Email"},
   "backendDOMNodeId":12},
  {"nodeId":"13","role":{"value":"LabelText"},"childIds":["14","15"],
   "backendDOMNodeId":13},
  {"nodeId":"14","role":{"value":"StaticText"},"name":{"value":"Plan "},
   "backendDOMNodeId":14},
  {"nodeId":"15","role":{"value":"combobox"},"name":{"value":"Plan"},
   "value":{"type":"string","value":"Free"},
   "properties":[{"name":"expanded","value":{"type":"booleanOrUndefined","value":false}},
                 {"name":"required","value":{"type":"boolean","value":false}}],
   "childIds":["16"],"backendDOMNodeId":15},
  {"nodeId":"16","role":{"value":"MenuListPopup"},"childIds":["17","18"]},
  {"nodeId":"17","role":{"value":"option"},"name":{"value":"Free"},
   "properties":[{"name":"selected","value":{"type":"booleanOrUndefined","value":true}}],
   "backendDOMNodeId":17},
  {"nodeId":"18","role":{"value":"option"},"name":{"value":"Pro"},
   "properties":[{"name":"selected","value":{"type":"booleanOrUndefined","value":false}},
                 {"name":"disabled","value":{"type":"boolean","value":true}}],
   "backendDOMNodeId":18},
  {"nodeId":"20","role":{"value":"button"},"name":{"value":"Send"},
   "properties":[{"name":"disabled","value":{"type":"boolean","value":true}}],
   "childIds":["21"],"backendDOMNodeId":20},
  {"nodeId":"21","role":{"value":"StaticText"},"name":{"value":"Send"},
   "backendDOMNodeId":21}
]})";

TEST(KoanCliSnapshotTest, CompactJoinsTextAndShowsStates) {
  const base::DictValue tree = Dict(kFormTree);
  const Snapshot compact = BuildSnapshot(tree, {});
  EXPECT_EQ(compact.text,
            "RootWebArea \"Sign up\" @1\n"
            "  main @2\n"
            "    heading \"Sign up\" [level 1] @3\n"
            "    \"Fill in all fields please.\"\n"
            "    radio \"Email\" [checked] @11\n"
            "    combobox \"Plan\" = \"Free\" [collapsed] @15\n"
            "      option \"Free\" [selected] @17\n"
            "      option \"Pro\" [disabled] @18\n"
            "    button \"Send\" [disabled] @20\n");

  // --full: the raw tree, inline text boxes and every run, a ref on each
  // node; the same refs.
  const Snapshot full = BuildSnapshot(tree, {.full = true});
  EXPECT_NE(full.text.find("      StaticText \"Sign up\" @31\n"
                           "        InlineTextBox \"Sign up\"\n"),
            std::string::npos)
      << full.text;
  EXPECT_NE(full.text.find("    LabelText @10\n"
                           "      radio \"Email\" [checked] @11\n"),
            std::string::npos);
  EXPECT_NE(full.text.find("StaticText \"all\" @421"), std::string::npos);
  EXPECT_GT(std::count(full.text.begin(), full.text.end(), '\n'),
            std::count(compact.text.begin(), compact.text.end(), '\n') + 8);
  EXPECT_EQ(full.refs, compact.refs);
  EXPECT_TRUE(std::ranges::contains(compact.refs, 421));
}

// A named image, canvas, figure or SVG chart gets a ref in the normal
// snapshot (for `screenshot --clip @N`), never in --interactive; an unnamed
// one is not shown.
TEST(KoanCliSnapshotTest, NamedVisualsGetARef) {
  const base::DictValue tree = Dict(R"({"nodes":[
    {"nodeId":"1","role":{"value":"RootWebArea"},"name":{"value":"Report"},
     "childIds":["2","3","4","5","6","7"],"backendDOMNodeId":1},
    {"nodeId":"2","role":{"value":"image"},
     "name":{"value":"Sales by quarter"},"backendDOMNodeId":20},
    {"nodeId":"3","role":{"value":"Canvas"},"name":{"value":"Live chart"},
     "backendDOMNodeId":30},
    {"nodeId":"4","role":{"value":"figure"},"name":{"value":"Figure 1"},
     "backendDOMNodeId":40},
    {"nodeId":"5","role":{"value":"SvgRoot"},"name":{"value":"Map"},
     "backendDOMNodeId":50},
    {"nodeId":"6","role":{"value":"image"},"backendDOMNodeId":60},
    {"nodeId":"7","role":{"value":"table"},"name":{"value":"Totals"},
     "backendDOMNodeId":70}
  ]})");
  EXPECT_EQ(BuildSnapshot(tree, {}).text,
            "RootWebArea \"Report\" @1\n"
            "  image \"Sales by quarter\" @20\n"
            "  Canvas \"Live chart\" @30\n"
            "  figure \"Figure 1\" @40\n"
            "  SvgRoot \"Map\" @50\n"
            "  table \"Totals\"\n");
  EXPECT_EQ(BuildSnapshot(tree, {.interactive = true}).text,
            "RootWebArea \"Report\" @1\n");
}

TEST(KoanCliSnapshotTest, Text) {
  // Two blocks do not run into each other; two wrappers (divs or spans) are
  // two words, but not before punctuation; inline elements flow on.
  const base::DictValue tree = Dict(R"({"nodes":[
    {"nodeId":"1","role":{"value":"RootWebArea"},"childIds":["2","3","4","5"]},
    {"nodeId":"2","role":{"value":"generic"},"childIds":["21"]},
    {"nodeId":"21","role":{"value":"StaticText"},"name":{"value":"One"}},
    {"nodeId":"3","role":{"value":"generic"},"childIds":["31","32"]},
    {"nodeId":"31","role":{"value":"StaticText"},"name":{"value":"Two"}},
    {"nodeId":"32","role":{"value":"strong"},"childIds":["321"]},
    {"nodeId":"321","role":{"value":"StaticText"},"name":{"value":"fold"}},
    {"nodeId":"4","role":{"value":"paragraph"},"childIds":["41","42","43"]},
    {"nodeId":"41","role":{"value":"StaticText"},"name":{"value":"Three"}},
    {"nodeId":"42","role":{"value":"generic"},"childIds":["421"]},
    {"nodeId":"421","role":{"value":"StaticText"},"name":{"value":"(four"}},
    {"nodeId":"43","role":{"value":"StaticText"},"name":{"value":")."}},
    {"nodeId":"5","role":{"value":"link"},"name":{"value":"Home"},
     "childIds":["51"],"backendDOMNodeId":5},
    {"nodeId":"51","role":{"value":"StaticText"},"name":{"value":"Home"}}
  ]})");
  EXPECT_EQ(BuildSnapshot(tree, {}).text,
            "RootWebArea\n"
            "  \"One Twofold\"\n"
            "  \"Three (four).\"\n"
            "  link \"Home\" @5\n");
}

TEST(KoanCliSnapshotTest, States) {
  const base::DictValue node = Dict(R"({"role":{"value":"checkbox"},
    "properties":[
      {"name":"checked","value":{"type":"tristate","value":"mixed"}},
      {"name":"expanded","value":{"type":"booleanOrUndefined","value":true}},
      {"name":"readonly","value":{"type":"boolean","value":false}},
      {"name":"required","value":{"type":"boolean","value":true}},
      {"name":"invalid","value":{"type":"token","value":"true"}},
      {"name":"focused","value":{"type":"booleanOrUndefined","value":true}},
      {"name":"level","value":{"type":"integer","value":2}}]})");
  EXPECT_EQ(StatesText(node), "[mixed, expanded, required, invalid, focused]");
  EXPECT_EQ(StatesText(Dict(R"({"role":{"value":"button"},"properties":[
      {"name":"pressed","value":{"type":"tristate","value":"true"}},
      {"name":"invalid","value":{"type":"token","value":"false"}}]})")),
            "[pressed]");
  EXPECT_EQ(StatesText(Dict(R"({"role":{"value":"radio"},"properties":[
      {"name":"checked","value":{"type":"tristate","value":"false"}}]})")),
            "");
  EXPECT_EQ(StatesText(base::DictValue()), "");
}

// A custom dropdown's list, rendered at the end of the body (a portal), is
// shown first, under its opener, and not again further down.
TEST(KoanCliSnapshotTest, OpenPopupsComeFirst) {
  auto tree = [](bool expanded, bool controls) {
    return Dict(std::string(R"({"nodes":[
      {"nodeId":"1","role":{"value":"RootWebArea"},"name":{"value":"Form"},
       "childIds":["2","9"],"backendDOMNodeId":1},
      {"nodeId":"2","role":{"value":"generic"},"childIds":["3","4"]},
      {"nodeId":"3","role":{"value":"combobox"},"name":{"value":"Country"},
       "backendDOMNodeId":30,"properties":[
         {"name":"expanded","value":{"type":"booleanOrUndefined","value":)") +
                (expanded ? "true" : "false") + "}}" +
                (controls ? R"(,{"name":"controls","value":{"type":"idrefList",
           "value":"","relatedNodes":[{"backendDOMNodeId":50,"idref":"l"}]}})"
                          : "") +
                R"(]},
      {"nodeId":"4","role":{"value":"StaticText"},"name":{"value":"Lots of text"}},
      {"nodeId":"9","role":{"value":"generic"},"childIds":["10"]},
      {"nodeId":"10","role":{"value":"listbox"},"name":{"value":"Country"},
       "backendDOMNodeId":50,"childIds":["11","12"]},
      {"nodeId":"11","role":{"value":"option"},"name":{"value":"India"},
       "backendDOMNodeId":51},
      {"nodeId":"12","role":{"value":"option"},"name":{"value":"Uganda"},
       "backendDOMNodeId":52}
    ]})");
  };
  EXPECT_EQ(BuildSnapshot(tree(true, true), {}).text,
            "popup of combobox \"Country\" @30 (open; shown first, the page "
            "has it further down):\n"
            "  listbox \"Country\" @50\n"
            "    option \"India\" @51\n"
            "    option \"Uganda\" @52\n"
            "RootWebArea \"Form\" @1\n"
            "  combobox \"Country\" [expanded] @30\n"
            "  \"Lots of text\"\n");
  // Not named by its control (react-select), but outside the page's
  // layout while something is expanded.
  EXPECT_EQ(BuildSnapshot(tree(true, false), {}).text.substr(0, 61),
            "popup (open; shown first, the page has it further down):\n"
            "  li");
  // Nothing expanded: a list that is always there stays where it is.
  EXPECT_EQ(BuildSnapshot(tree(false, false), {}).text,
            "RootWebArea \"Form\" @1\n"
            "  combobox \"Country\" [collapsed] @30\n"
            "  \"Lots of text\"\n"
            "  listbox \"Country\" @50\n"
            "    option \"India\" @51\n"
            "    option \"Uganda\" @52\n");
  // A long page cuts its end, never the popup.
  const Snapshot cut = BuildSnapshot(tree(true, true), {.max_lines = 4});
  EXPECT_NE(cut.text.find("option \"Uganda\" @52"), std::string::npos);
  EXPECT_NE(cut.text.find("(more; the page is long)"), std::string::npos);
  // --full keeps the tree's order.
  EXPECT_EQ(BuildSnapshot(tree(true, true), {.full = true}).text.find("popup"),
            std::string::npos);
}

TEST(KoanCliSnapshotTest, ADialogComesFirst) {
  const base::DictValue tree = Dict(R"({"nodes":[
    {"nodeId":"1","role":{"value":"RootWebArea"},"childIds":["2","3"],
     "backendDOMNodeId":1},
    {"nodeId":"2","role":{"value":"button"},"name":{"value":"Behind"},
     "backendDOMNodeId":2},
    {"nodeId":"3","role":{"value":"dialog"},"name":{"value":"Cookies"},
     "backendDOMNodeId":3,"childIds":["4"]},
    {"nodeId":"4","role":{"value":"button"},"name":{"value":"Accept"},
     "backendDOMNodeId":4}
  ]})");
  EXPECT_EQ(BuildSnapshot(tree, {}).text,
            "popup (open; shown first, the page has it further down):\n"
            "  dialog \"Cookies\" @3\n"
            "    button \"Accept\" @4\n"
            "RootWebArea @1\n"
            "  button \"Behind\" @2\n");
}

TEST(KoanCliSnapshotTest, RefsBelongToOnePage) {
  RefBook book;
  EXPECT_EQ(book.Check("T", "D1", 5), RefVerdict::kNoSnapshot);
  book.Remember("T", "D1", {5, 6});
  EXPECT_EQ(book.Check("T", "D1", 5), RefVerdict::kOk);
  EXPECT_EQ(book.Check("T", "D1", 7), RefVerdict::kUnknown);
  // The tab navigated: the same number names a node of another page.
  EXPECT_EQ(book.Check("T", "D2", 5), RefVerdict::kOtherDocument);
  EXPECT_EQ(book.Check("T", "", 5), RefVerdict::kOtherDocument);
  // Another tab's refs are not this one's.
  EXPECT_EQ(book.Check("U", "D1", 5), RefVerdict::kNoSnapshot);
  // A second snapshot of the same page adds to the first (--full and compact
  // share refs).
  book.Remember("T", "D1", {7});
  EXPECT_EQ(book.Check("T", "D1", 5), RefVerdict::kOk);
  EXPECT_EQ(book.Check("T", "D1", 7), RefVerdict::kOk);
  // Of a new page, it replaces them.
  book.Remember("T", "D2", {8});
  EXPECT_EQ(book.Check("T", "D2", 5), RefVerdict::kUnknown);
  EXPECT_EQ(book.Check("T", "D2", 8), RefVerdict::kOk);
  EXPECT_EQ(book.Check("T", "D1", 8), RefVerdict::kOtherDocument);
  book.Forget("T");
  EXPECT_EQ(book.Check("T", "D2", 8), RefVerdict::kNoSnapshot);

  for (RefVerdict verdict :
       {RefVerdict::kOk, RefVerdict::kNoSnapshot, RefVerdict::kOtherDocument,
        RefVerdict::kUnknown}) {
    EXPECT_EQ(RefVerdictFromName(RefVerdictName(verdict)), verdict);
  }
  EXPECT_FALSE(RefVerdictFromName("maybe"));
  const std::string refusal =
      RefRefusal(RefVerdict::kOtherDocument, "@5", "abcd");
  EXPECT_TRUE(refusal.starts_with("Koan.staleRef: @5 "));
  EXPECT_NE(refusal.find("koan snapshot abcd"), std::string::npos);
  EXPECT_EQ(RefRefusal(RefVerdict::kOk, "@5", "abcd"), "");
}

// Auto-resume (design 9.2): refs from before the user had the tab are stale
// once it resumes, whatever document it shows, until a new snapshot.
TEST(KoanCliSnapshotTest, AResumeMakesRefsStale) {
  RefBook book;
  book.Remember("T", "D1", {5, 6});
  book.Remember("U", "D1", {5});
  book.Resumed("T");
  EXPECT_EQ(book.Check("T", "D1", 5), RefVerdict::kResumed);
  EXPECT_EQ(book.Check("T", "D2", 5), RefVerdict::kResumed);
  // Another tab keeps its refs; a tab with no snapshot has none to lose.
  EXPECT_EQ(book.Check("U", "D1", 5), RefVerdict::kOk);
  book.Resumed("V");
  EXPECT_EQ(book.Check("V", "D1", 5), RefVerdict::kNoSnapshot);
  // The next snapshot, of the same document too, starts again.
  book.Remember("T", "D1", {7});
  EXPECT_EQ(book.Check("T", "D1", 7), RefVerdict::kOk);
  EXPECT_EQ(book.Check("T", "D1", 5), RefVerdict::kUnknown);
  EXPECT_EQ(RefVerdictFromName(RefVerdictName(RefVerdict::kResumed)),
            RefVerdict::kResumed);
  const std::string refusal = RefRefusal(RefVerdict::kResumed, "@5", "abcd");
  EXPECT_TRUE(refusal.starts_with("Koan.staleRef: @5 "));
  EXPECT_NE(refusal.find("before the user used this tab"), std::string::npos);
  EXPECT_NE(refusal.find("koan snapshot abcd"), std::string::npos);
}

// --- Actions -----------------------------------------------------------------

base::Value Returned(std::string_view json) {
  return std::move(*base::JSONReader::Read(json, base::JSON_PARSE_RFC));
}

TEST(KoanCliActionsTest, ReturnedValues) {
  EXPECT_EQ(*ReturnedValue(Dict(R"({"result":{"type":"string","value":"x"}})")),
            base::Value("x"));
  EXPECT_FALSE(ReturnedValue(Dict(
      R"({"result":{"type":"object"},"exceptionDetails":{"text":"boom"}})")));
  EXPECT_FALSE(ReturnedValue(Dict(R"({"result":{"type":"undefined"}})")));
}

TEST(KoanCliActionsTest, Select) {
  base::Value chosen = Returned(
      R"({"ok":true,"already":false,"held":true,"label":"Pro","value":"pro"})");
  SelectOutcome outcome = ReadSelectOutcome(&chosen, "@7");
  EXPECT_TRUE(outcome.ok);
  EXPECT_EQ(outcome.text, "ok: selected \"Pro\"");
  EXPECT_EQ(outcome.value, "pro");

  base::Value already = Returned(
      R"({"ok":true,"already":true,"held":true,"label":"Pro","value":"pro"})");
  EXPECT_EQ(ReadSelectOutcome(&already, "@7").text,
            "ok: \"Pro\" was already selected");

  // Never "ok" when it did not take.
  base::Value put_back = Returned(
      R"({"ok":true,"already":false,"held":false,"label":"Pro","value":"pro"})");
  outcome = ReadSelectOutcome(&put_back, "@7");
  EXPECT_FALSE(outcome.ok);
  EXPECT_TRUE(outcome.text.starts_with("Koan.notSelected: "));

  base::Value missing =
      Returned(R"({"error":"noOption","options":["Free","Pro"],"more":3})");
  EXPECT_EQ(ReadSelectOutcome(&missing, "@7").text,
            "Koan.noOption: @7 has no option with that label or value; its "
            "options: \"Free\", \"Pro\" and 3 more");
  base::Value not_select =
      Returned(R"({"error":"notSelect","what":"<input>"})");
  EXPECT_TRUE(IsNotSelect(&not_select));
  EXPECT_TRUE(ReadSelectOutcome(&not_select, "@7")
                  .text.starts_with("Koan.notSelect: @7 is <input>, "));
  base::Value disabled = Returned(R"({"error":"disabled","label":"Gold"})");
  EXPECT_TRUE(ReadSelectOutcome(&disabled, "@7")
                  .text.starts_with("Koan.disabled: \"Gold\""));
  EXPECT_FALSE(IsNotSelect(&disabled));
  EXPECT_FALSE(ReadSelectOutcome(nullptr, "@7").ok);
  EXPECT_FALSE(IsNotSelect(nullptr));
}

TEST(KoanCliActionsTest, Fill) {
  base::Value text = Returned(R"({"kind":"text","before":"old"})");
  FillTarget target = ReadFillTarget(&text, "@3");
  EXPECT_EQ(target.kind, FillKind::kText);
  EXPECT_EQ(target.before, "old");
  EXPECT_FALSE(target.secret);
  base::Value password =
      Returned(R"({"kind":"text","before":"","secret":true})");
  EXPECT_TRUE(ReadFillTarget(&password, "@3").secret);
  base::Value select = Returned(R"({"kind":"select"})");
  EXPECT_EQ(ReadFillTarget(&select, "@3").kind, FillKind::kSelect);
  base::Value checkbox =
      Returned(R"({"kind":"notText","what":"<input type=checkbox>"})");
  target = ReadFillTarget(&checkbox, "@3");
  EXPECT_EQ(target.kind, FillKind::kRefused);
  EXPECT_TRUE(target.refusal.starts_with(
      "Koan.notEditable: @3 is <input type=checkbox>, "));
  base::Value disabled = Returned(R"({"kind":"disabled","what":"<input>"})");
  EXPECT_TRUE(
      ReadFillTarget(&disabled, "@3").refusal.starts_with("Koan.disabled: "));
  EXPECT_EQ(ReadFillTarget(nullptr, "@3").kind, FillKind::kRefused);

  EXPECT_EQ(CheckFill("old", "new", "new"), FillOutcome::kFilled);
  EXPECT_EQ(CheckFill("", "(555) 123", "555123"), FillOutcome::kAltered);
  EXPECT_EQ(CheckFill("", "", "abc"), FillOutcome::kUnchanged);
  EXPECT_EQ(CheckFill("x", "", ""), FillOutcome::kFilled);
  EXPECT_EQ(CheckFill("x", "x", ""), FillOutcome::kUnchanged);
  EXPECT_EQ(FillOutcomeText(FillOutcome::kFilled, "@3", "new", false), "ok");
  EXPECT_EQ(FillOutcomeText(FillOutcome::kAltered, "@3", "(555) 123", false),
            "ok: @3 now holds \"(555) 123\" (the page changed what was typed)");
  const std::string unchanged =
      FillOutcomeText(FillOutcome::kUnchanged, "@3", "", false);
  EXPECT_TRUE(unchanged.starts_with("Koan.notFilled: @3 still holds \"\": "))
      << unchanged;
  // A password field's content is never printed.
  EXPECT_EQ(FillOutcomeText(FillOutcome::kAltered, "@3", "hunter2", true)
                .find("hunter2"),
            std::string::npos);
}

TEST(KoanCliActionsTest, ErrorsAgentsAreShown) {
  // Koan's own pass as they are.
  AgentError error = MapError("Koan.notGranted", "only the user can");
  EXPECT_EQ(error.code, "Koan.notGranted");
  EXPECT_EQ(error.message, "only the user can");
  EXPECT_EQ(MapError("Hold.refused", "x").code, "Hold.refused");
  // Raw CDP errors become Koan's, with what to do.
  error = MapError("-32000", "No node with given id found");
  EXPECT_EQ(error.code, "Koan.staleRef");
  EXPECT_NE(error.message.find("take a new snapshot"), std::string::npos);
  EXPECT_NE(error.message.find("(No node with given id found)"),
            std::string::npos);
  error = MapError("-32000", "Node does not have a layout object");
  EXPECT_EQ(error.code, "Koan.notVisible");
  EXPECT_NE(error.message.find("select @ref"), std::string::npos);
  EXPECT_EQ(MapError("-32000", "Could not compute content quads.").code,
            "Koan.notVisible");
  EXPECT_EQ(MapError("-32000", "Element is not focusable").code,
            "Koan.notFocusable");
  EXPECT_EQ(MapError("-32000", "Cannot find context with specified id").code,
            "Koan.pageChanged");
  EXPECT_EQ(MapError("-32000", "Cannot navigate to invalid URL").code,
            "Koan.badUrl");
  EXPECT_EQ(MapError("-32601", "'DOM.frob' wasn't found").code,
            "Koan.notSupported");
  // Anything else still says what happened.
  error = MapError("Failed to deserialize params.url", "Invalid parameters");
  EXPECT_EQ(error.code, "Koan.pageError");
  EXPECT_NE(error.message.find("Invalid parameters: Failed to deserialize"),
            std::string::npos);
  error = MapError("-32000", "Something odd");
  EXPECT_EQ(error.code, "Koan.pageError");
  EXPECT_NE(error.message.find("(Something odd)"), std::string::npos);
  EXPECT_EQ(error.message.find("-32000"), std::string::npos);
}

TEST(KoanCliActionsTest, ChoosingFromAList) {
  OptionList list;
  list.found = true;
  list.labels = {"United States", "United Kingdom", "Uganda", "India",
                 "United Kingdom"};
  list.disabled = {false, false, true, false, false};
  EXPECT_EQ(MatchOption(list, "United Kingdom", "@7").index, 1u);
  EXPECT_EQ(MatchOption(list, "  united   kingdom ", "@7").index, 1u);
  // Partial, if only one fits: starting with it, else containing it.
  EXPECT_EQ(MatchOption(list, "Ind", "@7").index, 3u);
  EXPECT_EQ(MatchOption(list, "dia", "@7").index, 3u);
  OptionMatch several = MatchOption(list, "United", "@7");
  EXPECT_FALSE(several.index);
  EXPECT_EQ(several.error,
            "Koan.ambiguousOption: several options of @7 start with "
            "\"United\": \"United States\", \"United Kingdom\", \"United "
            "Kingdom\"; give the whole label");
  OptionMatch disabled = MatchOption(list, "Uganda", "@7");
  EXPECT_EQ(disabled.error, "Koan.disabled: \"Uganda\" in @7 is disabled");
  list.more = 3;
  OptionMatch none = MatchOption(list, "France", "@7");
  EXPECT_EQ(none.error,
            "Koan.noOption: @7 has no option \"France\"; its options: "
            "\"United States\", \"United Kingdom\", \"Uganda\", \"India\", "
            "\"United Kingdom\" and 3 more");
  EXPECT_FALSE(MatchOption(OptionList(), "x", "@7").index);
  EXPECT_NE(NoListText("@7").find("Koan.noList"), std::string::npos);
  EXPECT_EQ(NotChosenText("@7", "Uganda", ""),
            "Koan.notChosen: \"Uganda\" was clicked, but @7 shows nothing; "
            "take a snapshot to see the list, and click the option's ref");
}

TEST(KoanCliActionsTest, ChooseAndClickAnswers) {
  auto value = [](std::string_view json) { return base::Value(Dict(json)); };
  base::Value select = value(R"({"kind":"select"})");
  EXPECT_EQ(ReadChooseTarget(&select, "@1").kind, ChooseKind::kSelect);
  base::Value popup =
      value(R"({"kind":"popup","expanded":true,"editable":true})");
  ChooseTarget target = ReadChooseTarget(&popup, "@1");
  EXPECT_EQ(target.kind, ChooseKind::kPopup);
  EXPECT_TRUE(target.expanded);
  EXPECT_TRUE(target.editable);
  base::Value other = value(R"({"kind":"other","what":"<p>"})");
  EXPECT_NE(ReadChooseTarget(&other, "@1")
                .refusal.find("Koan.notSelect: @1 "
                              "is <p>"),
            std::string::npos);
  EXPECT_EQ(ReadChooseTarget(nullptr, "@1").kind, ChooseKind::kRefused);

  base::Value listed = value(R"({"found":true,"mine":true,"more":2,"options":[
      {"label":"A","disabled":false},{"label":"B","disabled":true}]})");
  OptionList list = ReadOptionList(&listed);
  EXPECT_TRUE(list.found);
  EXPECT_TRUE(list.mine);
  EXPECT_EQ(list.labels, (std::vector<std::string>{"A", "B"}));
  EXPECT_EQ(list.disabled, (std::vector<bool>{false, true}));
  EXPECT_EQ(list.more, 2);
  EXPECT_FALSE(ReadOptionList(nullptr).found);

  base::Value covered =
      value(R"({"stable":false,"covered":"<div class=\"backdrop\">"})");
  ClickReadiness ready = ReadClickReadiness(&covered);
  EXPECT_FALSE(ready.stable);
  EXPECT_EQ(ready.covered, "<div class=\"backdrop\">");
  // A page that does not answer: click as before.
  EXPECT_TRUE(ReadClickReadiness(nullptr).stable);
  EXPECT_TRUE(ReadClickReadiness(nullptr).covered.empty());
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
  EXPECT_EQ(DecideLocalClient(koan_, {claude_}, 501, Peer(koan_, claude_)),
            LocalClientVerdict::kAllowed);
  // Another program of the same user: a script, a copy of koan elsewhere,
  // koan with another signature.
  EXPECT_EQ(DecideLocalClient(koan_, {claude_}, 501,
                              Peer({"/usr/bin/python3", ""}, claude_)),
            LocalClientVerdict::kOtherProgram);
  EXPECT_EQ(DecideLocalClient(koan_, {claude_}, 501,
                              Peer({"/tmp/koan", "ad-hoc:abc"}, claude_)),
            LocalClientVerdict::kOtherProgram);
  EXPECT_EQ(DecideLocalClient(koan_, {claude_}, 501,
                              Peer({koan_.path, "ad-hoc:evil"}, claude_)),
            LocalClientVerdict::kOtherProgram);
  // The right koan, run by something other than the paired agent.
  EXPECT_EQ(DecideLocalClient(koan_, {claude_}, 501,
                              Peer(koan_, {"/Applications/Other.app/o", "X"})),
            LocalClientVerdict::kAgentMissing);
  // Another user, or nobody the OS could name.
  EXPECT_EQ(DecideLocalClient(koan_, {claude_}, 501, Peer(koan_, claude_, 502)),
            LocalClientVerdict::kOtherUser);
  EXPECT_EQ(DecideLocalClient(koan_, {claude_}, 501, std::nullopt),
            LocalClientVerdict::kUnresolved);
  EXPECT_EQ(DecideLocalClient(koan_, {claude_}, 501, Peer({"", ""}, claude_)),
            LocalClientVerdict::kUnresolved);
  // A holder that could not name itself serves nobody.
  EXPECT_EQ(DecideLocalClient({"", ""}, {claude_}, 501, Peer(koan_, claude_)),
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
  EXPECT_EQ(DecideLocalClient(koan_, {agent::ProcessInfo()}, 501, orphan),
            LocalClientVerdict::kAllowed);
  EXPECT_EQ(DecideLocalClient(koan_, {agent::ProcessInfo()}, 501,
                              Peer(koan_, claude_)),
            LocalClientVerdict::kAgentMissing);
  EXPECT_EQ(VerdictName(LocalClientVerdict::kAgentMissing), "agent-missing");
}

// The pass of 2026-11-11: `koan pair` was started by a launcher that had
// exited by the time the user clicked Allow, so its record (read then, by
// that koan) had no agent, while the browser had pinned the agent at connect
// and let the holder in. Every later command was then turned away as
// agent-missing.
TEST_F(KoanCliAdmissionTest, TheHolderServesTheAgentTheBrowserLetIn) {
  const agent::ProcessInfo sshd{"/usr/libexec/sshd-session", "apple"};
  // The holder, started by `koan open` from the agent's shell.
  agent::PeerIdentity holder;
  holder.uid = 501;
  holder.process = koan_;
  holder.ancestors = {koan_, {"/bin/zsh", "com.apple.zsh"}, sshd};

  // A record whose pin has no agent: the agent that started the holder is
  // served too, and its commands are let in.
  const agent::PinnedIdentity unknown{koan_, agent::ProcessInfo()};
  const std::vector<agent::ProcessInfo> served = AgentsToServe(unknown, holder);
  EXPECT_EQ(served, (std::vector<agent::ProcessInfo>{{}, sshd}));
  EXPECT_EQ(DecideLocalClient(koan_, served, 501, Peer(koan_, sshd)),
            LocalClientVerdict::kAllowed);
  EXPECT_EQ(DecideLocalClient(koan_, served, 501, Peer(koan_, claude_)),
            LocalClientVerdict::kAgentMissing);

  // The pinned agent is in the holder's chain: only that one.
  EXPECT_EQ(AgentsToServe(agent::PinnedIdentity{koan_, sshd}, holder),
            std::vector<agent::ProcessInfo>{sshd});
  // A changed chain the user approved in the browser: the paired agent and
  // the one that started the holder.
  EXPECT_EQ(AgentsToServe(agent::PinnedIdentity{koan_, claude_}, holder),
            (std::vector<agent::ProcessInfo>{claude_, sshd}));
  // A record without a pin (an older koan): the agent that started it.
  EXPECT_EQ(AgentsToServe(std::nullopt, holder),
            std::vector<agent::ProcessInfo>{sshd});

  // Nothing served, nobody let in; another program is still another program.
  EXPECT_EQ(DecideLocalClient(koan_, {}, 501, Peer(koan_, sshd)),
            LocalClientVerdict::kAgentMissing);
  EXPECT_EQ(DecideLocalClient(koan_, served, 501,
                              Peer({"/usr/bin/python3", ""}, sshd)),
            LocalClientVerdict::kOtherProgram);
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

// The holder makes the refs of a resumed tab stale (RefBook::Resumed()).
TEST(KoanCliRouterTest, ResumedTabsAreNoted) {
  Router router;
  router.FromBrowser(R"({"method":"Koan.paused","params":{"targetId":"T1"}})");
  EXPECT_TRUE(router.TakeResumed().empty());
  router.FromBrowser(R"({"method":"Koan.resumed",
                         "params":{"targetId":"T1","reason":"user-idle"}})");
  router.FromBrowser(R"({"method":"Koan.resumed","params":{}})");
  router.FromBrowser(R"({"method":"Koan.resumed",
                         "params":{"targetId":"T2","reason":"handed-back"}})");
  EXPECT_EQ(router.TakeResumed(), (std::vector<std::string>{"T1", "T2"}));
  EXPECT_TRUE(router.TakeResumed().empty());
}

TEST(KoanCliRouterTest, AFollowerGetsItsTabsEvents) {
  Router router;
  auto to = [&](std::string_view json) {
    std::vector<Router::ConnectionId> ids;
    for (const Router::Delivery& delivery : router.FromBrowser(json)) {
      ids.push_back(delivery.connection);
    }
    return ids;
  };
  using Ids = std::vector<Router::ConnectionId>;
  constexpr char kS1[] =
      R"({"method":"Network.requestWillBeSent","params":{},"sessionId":"S1"})";
  constexpr char kS2[] =
      R"({"method":"Page.loadEventFired","params":{},"sessionId":"S2"})";
  constexpr char kPaused[] = R"({"method":"Koan.paused","params":{}})";
  router.Follow(3, "S1");
  EXPECT_EQ(to(kS1), Ids{3});
  EXPECT_TRUE(to(kS2).empty());
  EXPECT_EQ(to(kPaused), Ids{3});
  // A watcher that follows too gets each event once.
  router.AddWatcher(4);
  router.Follow(4, "S2");
  EXPECT_EQ(to(kPaused), (Ids{4, 3}));
  EXPECT_EQ(to(kS2), Ids{4});
  router.Follow(3, "");
  EXPECT_TRUE(to(kS1).empty());
  router.RemoveConnection(4);
  EXPECT_TRUE(to(kS2).empty());
}

}  // namespace
}  // namespace koan::cli
