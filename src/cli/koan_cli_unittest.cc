// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/files/scoped_file.h"
#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/cli/koan_cli_actions.h"
#include "koan/cli/koan_cli_admission.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_client.h"
#include "koan/cli/koan_cli_io.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_cli_router.h"
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

}  // namespace
}  // namespace koan::cli
