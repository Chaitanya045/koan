// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_batch.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_frame.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_cli_skill.h"
#include "koan/cli/koan_cli_snapshot.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::cli {
namespace {

base::DictValue Dict(std::string_view json) {
  return std::move(*base::JSONReader::Read(json, base::JSON_PARSE_RFC))
      .TakeDict();
}

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

Message Event(std::string_view json) {
  return *ParseMessage(json);
}

// --- Parsing a batch ---------------------------------------------------------

TEST(KoanCliBatchTest, StepsInOrderWithOptionsAnywhere) {
  Command batch = Parse({"act", "ab12", "fill", "@12", "Ada", "fill", "@13",
                         "Lovelace", "choose", "@20", "UK", "click", "@31",
                         "--until", "url~/step-2", "--snapshot"});
  ASSERT_EQ(batch.steps.size(), 4u);
  EXPECT_EQ(batch.steps[0].act, ActKind::kFill);
  EXPECT_EQ(batch.steps[0].target, "@12");
  EXPECT_EQ(batch.steps[0].text, "Ada");
  EXPECT_EQ(batch.steps[1].text, "Lovelace");
  EXPECT_EQ(batch.steps[2].act, ActKind::kChoose);
  EXPECT_EQ(batch.steps[2].text, "UK");
  EXPECT_EQ(batch.steps[3].act, ActKind::kClick);
  EXPECT_EQ(batch.steps[3].target, "@31");
  ASSERT_TRUE(batch.until);
  EXPECT_EQ(batch.until->kind, Until::kUrl);
  EXPECT_EQ(batch.until->needle, "/step-2");
  EXPECT_TRUE(batch.snapshot_after);
  // The first step is the one a single act runs.
  EXPECT_EQ(batch.act, ActKind::kFill);
  EXPECT_EQ(batch.ref, "@12");

  // A text that looks like an option is the text; steps go on after an
  // option.
  Command mixed = Parse({"act", "ab12", "fill", "@3", "--diff", "--diff",
                         "press", "Enter", "scroll", "-400"});
  ASSERT_EQ(mixed.steps.size(), 3u);
  EXPECT_EQ(mixed.steps[0].text, "--diff");
  EXPECT_TRUE(mixed.diff);
  EXPECT_TRUE(mixed.snapshot_after);
  EXPECT_EQ(mixed.steps[1].text, "Enter");
  EXPECT_EQ(mixed.steps[2].scroll_by, -400);

  // Locators where a ref goes.
  Command located =
      Parse({"act", "ab12", "click", "loc:role=button[name=\"Continue\"]",
             "navigate", "https://a.test/"});
  ASSERT_EQ(located.steps.size(), 2u);
  EXPECT_EQ(located.steps[0].target, "loc:role=button[name=\"Continue\"]");
  EXPECT_EQ(located.steps[1].act, ActKind::kNavigate);
}

TEST(KoanCliBatchTest, MultiFieldFill) {
  Command fill = Parse({"act", "ab12", "fill", "@12=Ada", "@13=Love=lace",
                        "@14=", "click", "@31"});
  ASSERT_EQ(fill.steps.size(), 4u);
  EXPECT_EQ(fill.steps[0].target, "@12");
  EXPECT_EQ(fill.steps[0].text, "Ada");
  // Only the first "=" splits.
  EXPECT_EQ(fill.steps[1].text, "Love=lace");
  // An empty text clears the field.
  EXPECT_EQ(fill.steps[2].text, "");
  EXPECT_EQ(fill.steps[3].act, ActKind::kClick);
  // The usual form still works, and "=" in a text is the text.
  Command plain = Parse({"act", "ab12", "fill", "@12", "a=b"});
  ASSERT_EQ(plain.steps.size(), 1u);
  EXPECT_EQ(plain.steps[0].text, "a=b");
}

TEST(KoanCliBatchTest, BadBatchesAreRefused) {
  EXPECT_NE(ErrorOf({"act", "ab12", "click", "@1", "wiggle"})
                .find("unknown action \"wiggle\""),
            std::string::npos);
  EXPECT_NE(ErrorOf({"act", "ab12", "fill", "@1"}).find("usage: fill"),
            std::string::npos);
  EXPECT_NE(ErrorOf({"act", "ab12", "click", "loc:button"}).find("click"),
            std::string::npos);
  EXPECT_NE(ErrorOf({"act", "ab12"}).find("usage"), std::string::npos);
  EXPECT_NE(ErrorOf({"act", "ab12", "click", "@1", "--steps", "f.txt"})
                .find("not both"),
            std::string::npos);
  // The cap, with a clear message.
  std::vector<std::string> many = {"act", "ab12"};
  for (size_t i = 0; i <= kMaxSteps; ++i) {
    many.push_back("press");
    many.push_back("Tab");
  }
  EXPECT_NE(ErrorOf(many).find("at most 50 steps"), std::string::npos);
  many.resize(2 + 2 * kMaxSteps);
  EXPECT_EQ(Parse(many).steps.size(), kMaxSteps);
  // Options of act and snapshot stay theirs.
  EXPECT_NE(ErrorOf({"tabs", "--until", "url~x"}).find("koan act's"),
            std::string::npos);
  EXPECT_NE(ErrorOf({"tabs", "--interactive"}).find("koan snapshot's"),
            std::string::npos);
  EXPECT_FALSE(ErrorOf({"snapshot", "ab12", "--scope", "half"}).empty());
  EXPECT_FALSE(ErrorOf({"snapshot", "ab12", "--full", "--diff"}).empty());
}

TEST(KoanCliBatchTest, SnapshotOptions) {
  Command snapshot = Parse({"snapshot", "ab12", "--interactive", "--diff",
                            "--scope", "@40", "--locators"});
  EXPECT_TRUE(snapshot.interactive);
  EXPECT_TRUE(snapshot.diff);
  EXPECT_TRUE(snapshot.locators);
  EXPECT_EQ(snapshot.scope, "@40");
  EXPECT_EQ(Parse({"snapshot", "ab12", "--scope=viewport"}).scope, "viewport");
  // On act they imply --snapshot.
  EXPECT_TRUE(
      Parse({"act", "ab12", "click", "@1", "--interactive"}).snapshot_after);
}

TEST(KoanCliBatchTest, StepsFile) {
  std::vector<Step> steps;
  std::string error;
  ASSERT_TRUE(
      ParseStepLines("# page one\n"
                     "fill @12 \"Ada Lovelace\"\n"
                     "\n"
                     "fill @13=x @14=y\n"
                     "koan click 'loc:role=button[name=\"Next\"]'\n",
                     &steps, &error))
      << error;
  ASSERT_EQ(steps.size(), 4u);
  EXPECT_EQ(steps[0].text, "Ada Lovelace");
  EXPECT_EQ(steps[2].target, "@14");
  EXPECT_EQ(steps[3].target, "loc:role=button[name=\"Next\"]");

  steps.clear();
  EXPECT_FALSE(ParseStepLines("click @1\nclick 2\n", &steps, &error));
  EXPECT_EQ(error.rfind("line 2: ", 0), 0u) << error;
  steps.clear();
  EXPECT_FALSE(ParseStepLines("click @1 --snapshot\n", &steps, &error));
  EXPECT_NE(error.find("command line"), std::string::npos);
  steps.clear();
  EXPECT_FALSE(ParseStepLines("# nothing\n", &steps, &error));
  EXPECT_FALSE(Parse({"act", "ab12", "--steps", "-"}).steps_file.empty());
}

TEST(KoanCliBatchTest, Receipts) {
  Step fill;
  fill.act = ActKind::kFill;
  fill.target = "@13";
  fill.text = "secret";
  Step click;
  click.target = "@31";
  Step key;
  key.act = ActKind::kPress;
  key.text = "Enter";
  EXPECT_EQ(ReceiptLine(1, 4, fill, "ok"), "2/4 fill @13: ok");
  // Never fill's text: it may be a password.
  EXPECT_EQ(StepLabel(fill).find("secret"), std::string::npos);
  EXPECT_EQ(StoppedLine(2, 4, click, "Koan.paused: x"),
            "stopped at 3/4 click @31: Koan.paused: x");
  EXPECT_EQ(NotRunLine({fill, click, key}, 0),
            "not run: 2/3 click @31, 3/3 press Enter");
  EXPECT_EQ(NotRunLine({fill, click}, 1), "");
}

// --- Stopping for the user
// -----------------------------------------------------

TEST(KoanCliBatchTest, TheGateStopsForTheUser) {
  BatchGate gate("T1");
  EXPECT_FALSE(gate.StopReason());
  // Another tab's events do not count.
  gate.OnEvent(Event(R"({"method":"Koan.paused","params":{"targetId":"T2",
                         "reason":"user-input"}})"));
  EXPECT_FALSE(gate.StopReason());
  gate.OnEvent(Event(R"({"method":"Koan.paused","params":{"targetId":"T1",
                         "reason":"user-input"}})"));
  ASSERT_TRUE(gate.StopReason());
  EXPECT_EQ(gate.StopReason()->rfind("Koan.paused: ", 0), 0u);
  EXPECT_NE(gate.StopReason()->find("user-input"), std::string::npos);
  gate.OnEvent(Event(R"({"method":"Koan.resumed","params":{"targetId":"T1",
                         "reason":"handed-back"}})"));
  EXPECT_FALSE(gate.StopReason());

  gate.OnEvent(Event(R"({"method":"Koan.userNeeded","params":{"targetId":"T1",
                         "reason":"permission"}})"));
  ASSERT_TRUE(gate.StopReason());
  EXPECT_EQ(gate.StopReason()->rfind("Koan.userNeeded: ", 0), 0u);
  gate.OnEvent(Event(R"({"method":"Koan.userNeededDone","params":{
                         "targetId":"T1","reason":"permission"}})"));
  EXPECT_FALSE(gate.StopReason());

  // The tab list is the latest word on a pause (an event may be on its way).
  base::DictValue paused = Dict(R"({"targetId":"T1","state":"paused"})");
  gate.OnTab(&paused);
  ASSERT_TRUE(gate.StopReason());
  base::DictValue driven = Dict(R"({"targetId":"T1","state":"driven"})");
  gate.OnTab(&driven);
  EXPECT_FALSE(gate.StopReason());
  gate.OnTab(nullptr);
  EXPECT_EQ(gate.StopReason()->rfind("Koan.tabGone", 0), 0u);
}

TEST(KoanCliBatchTest, WhatWaitedBeforeTheBatch) {
  base::ListValue held;
  held.Append(R"({"method":"Koan.userNeeded","params":{"targetId":"T1",
                  "reason":"dialog"}})");
  held.Append(R"({"method":"Koan.userNeeded","params":{"targetId":"T1",
                  "reason":"interstitial"}})");
  held.Append("not json");
  BatchGate waiting("T1");
  waiting.Replay(held);
  ASSERT_TRUE(waiting.StopReason());
  EXPECT_NE(waiting.StopReason()->find("dialog"), std::string::npos);
  // An interstitial never says it is done: an old one does not stop a batch
  // (the browser refuses input to an interstitial itself).
  EXPECT_EQ(waiting.StopReason()->find("interstitial"), std::string::npos);
  held.Append(R"({"method":"Koan.userNeededDone","params":{"targetId":"T1",
                  "reason":"dialog"}})");
  BatchGate done("T1");
  done.Replay(held);
  EXPECT_FALSE(done.StopReason());
}

// --- --until
// -------------------------------------------------------------------

TEST(KoanCliBatchTest, UntilConditions) {
  std::string error;
  std::optional<Until> url = ParseUntil("url~/step-2", &error);
  ASSERT_TRUE(url);
  EXPECT_TRUE(UrlMatches("https://jobs.test/apply/step-2?x=1", *url));
  EXPECT_FALSE(UrlMatches("https://jobs.test/apply/step-1", *url));
  EXPECT_EQ(UntilText(*url), "url~/step-2");

  std::optional<Until> text = ParseUntil("text~Saved!", &error);
  ASSERT_TRUE(text);
  EXPECT_EQ(text->kind, Until::kText);
  EXPECT_FALSE(UrlMatches("https://a.test/Saved!", *text));
  // The needle is JSON in the expression: a quote cannot end it.
  EXPECT_NE(PageTextExpression("a\"b").find(R"("a\"b")"), std::string::npos);

  std::optional<Until> element = ParseUntil("@12~Done", &error);
  ASSERT_TRUE(element);
  EXPECT_EQ(element->kind, Until::kRefText);
  EXPECT_EQ(element->ref, "@12");
  base::Value has(Dict(R"({"connected":true,"shown":true,"has":true})"));
  base::Value lacks(Dict(R"({"connected":true,"shown":true,"has":false})"));
  base::Value hidden(Dict(R"({"connected":true,"shown":false,"has":true})"));
  EXPECT_TRUE(ElementMeets(*element, &has, false));
  EXPECT_FALSE(ElementMeets(*element, &lacks, false));
  EXPECT_FALSE(ElementMeets(*element, &has, /*gone=*/true));

  std::optional<Until> gone = ParseUntil("@31 gone", &error);
  ASSERT_TRUE(gone);
  EXPECT_EQ(gone->kind, Until::kRefGone);
  EXPECT_EQ(UntilText(*gone), "@31 gone");
  EXPECT_FALSE(ElementMeets(*gone, &has, false));
  EXPECT_TRUE(ElementMeets(*gone, &hidden, false));
  EXPECT_TRUE(ElementMeets(*gone, nullptr, /*gone=*/true));

  for (const char* bad : {"url~", "@x gone", "title~a", "soon", "@12~"}) {
    EXPECT_FALSE(ParseUntil(bad, &error)) << bad;
    EXPECT_FALSE(error.empty());
  }
}

// --- Locators
// --------------------------------------------------------------------

constexpr char kButtonsTree[] = R"({"nodes":[
  {"nodeId":"1","role":{"value":"RootWebArea"},"name":{"value":"Apply"},
   "childIds":["2","3","4","5","6"],"backendDOMNodeId":1},
  {"nodeId":"2","role":{"value":"button"},"name":{"value":"Continue"},
   "backendDOMNodeId":31},
  {"nodeId":"3","role":{"value":"button"},"name":{"value":"Save"},
   "backendDOMNodeId":32},
  {"nodeId":"4","role":{"value":"button"},"name":{"value":"Save"},
   "backendDOMNodeId":33},
  {"nodeId":"5","role":{"value":"textbox"},"name":{"value":"First  name"},
   "backendDOMNodeId":12},
  {"nodeId":"6","ignored":true,"role":{"value":"button"},
   "name":{"value":"Continue"},"backendDOMNodeId":34}
]})";

TEST(KoanCliBatchTest, LocatorsMatchExactlyOne) {
  std::string error;
  std::optional<Locator> continue_button =
      ParseLocator("loc:role=button[name=\"Continue\"]", &error);
  ASSERT_TRUE(continue_button) << error;
  EXPECT_EQ(continue_button->role, "button");
  EXPECT_EQ(continue_button->name, "Continue");
  std::optional<Locator> quoted =
      ParseLocator(R"(loc:role=link[name="Say \"hi\""])", &error);
  ASSERT_TRUE(quoted);
  EXPECT_EQ(quoted->name, "Say \"hi\"");
  EXPECT_EQ(LocatorText("link", "Say \"hi\""),
            R"(loc:role=link[name="Say \"hi\""])");
  for (const char* bad :
       {"loc:button", "loc:role=", "loc:role=button[name=Continue]",
        "loc:role=button[name=\"Continue\"", "loc:role=button[x=\"y\"]"}) {
    EXPECT_FALSE(ParseLocator(bad, &error)) << bad;
  }

  const base::DictValue tree = Dict(kButtonsTree);
  // One: its node (the ignored copy does not count).
  LocatorMatch one = MatchLocator(tree, *continue_button, "loc:...");
  EXPECT_EQ(one.node, 31);
  // Whitespace in a name is collapsed, as the snapshot prints it.
  std::optional<Locator> first =
      ParseLocator("loc:role=textbox[name=\"First name\"]", &error);
  EXPECT_EQ(MatchLocator(tree, *first, "x").node, 12);
  // Several: refused, listing them.
  std::optional<Locator> save =
      ParseLocator("loc:role=button[name=\"Save\"]", &error);
  LocatorMatch several = MatchLocator(tree, *save, "loc:save");
  EXPECT_FALSE(several.node);
  EXPECT_EQ(several.error.rfind("Koan.ambiguous: loc:save matches 2", 0), 0u)
      << several.error;
  EXPECT_NE(several.error.find("button \"Save\" @32"), std::string::npos);
  EXPECT_NE(several.error.find("button \"Save\" @33"), std::string::npos);
  // None: refused, listing that role's elements.
  std::optional<Locator> next =
      ParseLocator("loc:role=button[name=\"Next\"]", &error);
  LocatorMatch none = MatchLocator(tree, *next, "x");
  EXPECT_FALSE(none.node);
  EXPECT_EQ(none.error.rfind("Koan.notFound: no button named \"Next\"", 0), 0u)
      << none.error;
  EXPECT_NE(none.error.find("button \"Continue\" @31"), std::string::npos);
  std::optional<Locator> slider = ParseLocator("loc:role=slider", &error);
  EXPECT_NE(MatchLocator(tree, *slider, "x").error.find("no element with role"),
            std::string::npos);
}

TEST(KoanCliBatchTest, TheSnapshotPrintsOnlyUniqueLocators) {
  const base::DictValue tree = Dict(kButtonsTree);
  const std::string text = BuildSnapshot(tree, {.locators = true}).text;
  EXPECT_NE(
      text.find(R"(button "Continue" @31 loc:role=button[name="Continue"])"),
      std::string::npos)
      << text;
  // Two "Save" buttons: a locator would not name one.
  EXPECT_NE(text.find("button \"Save\" @32\n"), std::string::npos) << text;
  EXPECT_EQ(BuildSnapshot(tree, {}).text.find("loc:"), std::string::npos);
}

// --- Cheaper snapshots
// ------------------------------------------------------------

// A form page: headings, text, a closed country dropdown with its options,
// a field, an alert after a failed submit, and a footer of links.
constexpr char kPageTree[] = R"({"nodes":[
  {"nodeId":"1","role":{"value":"RootWebArea"},"name":{"value":"Apply"},
   "childIds":["2","3","4","5","8","9","11"],"backendDOMNodeId":1},
  {"nodeId":"2","role":{"value":"heading"},"name":{"value":"Your details"},
   "properties":[{"name":"level","value":{"type":"integer","value":2}}],
   "backendDOMNodeId":2},
  {"nodeId":"3","role":{"value":"paragraph"},"childIds":["31"],
   "backendDOMNodeId":3},
  {"nodeId":"31","role":{"value":"StaticText"},
   "name":{"value":"We need these to contact you."},"backendDOMNodeId":31},
  {"nodeId":"4","role":{"value":"textbox"},"name":{"value":"Email"},
   "value":{"value":"ada@"},
   "properties":[{"name":"invalid","value":{"type":"token","value":"true"}}],
   "backendDOMNodeId":12},
  {"nodeId":"5","role":{"value":"combobox"},"name":{"value":"Country"},
   "value":{"value":"India"},
   "properties":[{"name":"expanded","value":{"type":"booleanOrUndefined","value":false}}],
   "childIds":["6"],"backendDOMNodeId":20},
  {"nodeId":"6","role":{"value":"MenuListPopup"},"childIds":["61","62"]},
  {"nodeId":"61","role":{"value":"option"},"name":{"value":"India"},
   "backendDOMNodeId":61},
  {"nodeId":"62","role":{"value":"option"},"name":{"value":"UK"},
   "backendDOMNodeId":62},
  {"nodeId":"8","role":{"value":"alert"},"childIds":["81"],
   "backendDOMNodeId":8},
  {"nodeId":"81","role":{"value":"StaticText"},
   "name":{"value":"Enter a valid email"},"backendDOMNodeId":81},
  {"nodeId":"9","role":{"value":"button"},"name":{"value":"Next"},
   "backendDOMNodeId":31000},
  {"nodeId":"11","role":{"value":"contentinfo"},"childIds":["111"],
   "backendDOMNodeId":11},
  {"nodeId":"111","role":{"value":"link"},"name":{"value":"Privacy"},
   "backendDOMNodeId":111}
]})";

TEST(KoanCliBatchTest, InteractiveKeepsWhatAnAgentActsOn) {
  const base::DictValue tree = Dict(kPageTree);
  const Snapshot compact = BuildSnapshot(tree, {});
  const Snapshot interactive = BuildSnapshot(tree, {.interactive = true});
  EXPECT_EQ(interactive.text,
            "RootWebArea \"Apply\" @1\n"
            "  heading \"Your details\" [level 2] @2\n"
            "  textbox \"Email\" = \"ada@\" [invalid] @12\n"
            "  combobox \"Country\" = \"India\" [collapsed] @20\n"
            "  \"Enter a valid email\"\n"
            "  button \"Next\" @31000\n"
            "  link \"Privacy\" @111\n");
  // The page's text, the closed dropdown's options and the landmark are
  // what compact adds.
  EXPECT_NE(compact.text.find("We need these"), std::string::npos);
  EXPECT_NE(compact.text.find("option \"UK\""), std::string::npos);
  EXPECT_NE(compact.text.find("contentinfo"), std::string::npos);
  EXPECT_LT(interactive.text.size(), compact.text.size());
  // The same refs, whichever outline.
  EXPECT_EQ(interactive.refs, compact.refs);
}

TEST(KoanCliBatchTest, ScopeToOneElement) {
  const base::DictValue tree = Dict(kPageTree);
  const Snapshot footer = BuildSnapshot(tree, {.root = 11});
  EXPECT_EQ(footer.text, "contentinfo @11\n  link \"Privacy\" @111\n");
  EXPECT_FALSE(footer.root_missing);
  EXPECT_TRUE(BuildSnapshot(tree, {.root = 999}).root_missing);
}

TEST(KoanCliBatchTest, ViewportCutsWithMarkers) {
  Snapshot snapshot;
  snapshot.lines = {{1, 0, "RootWebArea \"Long\" @1", true}};
  for (int i = 0; i < 10; ++i) {
    snapshot.lines.push_back(
        {100 + i, 1, "\"Paragraph " + base::NumberToString(i) + "\"", false});
  }
  EXPECT_EQ(RenderWindow(snapshot, 4, 7),
            "RootWebArea \"Long\" @1\n"
            "[more above: 3 nodes]\n"
            "  \"Paragraph 3\"\n"
            "  \"Paragraph 4\"\n"
            "  \"Paragraph 5\"\n"
            "[more below: 4 nodes]\n");
  // Nothing cut: no marker.
  EXPECT_EQ(RenderWindow(snapshot, 1, 11).find("[more"), std::string::npos);
  // Cut by the line limit too: "+".
  snapshot.truncated = true;
  EXPECT_NE(RenderWindow(snapshot, 1, 11).find("[more below: 0+ nodes]"),
            std::string::npos);
}

// Runs a ViewportSearch over lines whose tops are `tops` (viewport 0..100).
// Returns the [first, last] it found, and counts the probes.
std::pair<size_t, size_t> Search(const std::vector<double>& tops,
                                 size_t* probes) {
  ViewportSearch search(tops.size());
  *probes = 0;
  for (int round = 0; round < 20 && !search.done(); ++round) {
    for (size_t index : search.NextProbes()) {
      ++*probes;
      const double top = tops[index];
      search.Report(index, top < 0      ? ViewportSearch::Place::kAbove
                           : top >= 100 ? ViewportSearch::Place::kBelow
                                        : ViewportSearch::Place::kIn);
    }
  }
  EXPECT_TRUE(search.done());
  EXPECT_FALSE(search.empty());
  return {search.first(), search.last()};
}

TEST(KoanCliBatchTest, ViewportSearchMeasuresFewLines) {
  // 1500 lines 10 px apart, scrolled by 5000: lines 500 to 509 are shown.
  std::vector<double> tops;
  for (int i = 0; i < 1500; ++i) {
    tops.push_back(i * 10.0 - 5000);
  }
  size_t probes = 0;
  EXPECT_EQ(Search(tops, &probes), std::make_pair(size_t{500}, size_t{509}));
  EXPECT_LT(probes, 80u);
  // A fixed header (always at the top) at the start is passed over.
  tops[0] = tops[1] = 0;
  EXPECT_EQ(Search(tops, &probes), std::make_pair(size_t{500}, size_t{509}));
  // Not scrolled: from the first line.
  std::vector<double> top_of_page;
  for (int i = 0; i < 40; ++i) {
    top_of_page.push_back(i * 10.0);
  }
  EXPECT_EQ(Search(top_of_page, &probes), std::make_pair(size_t{0}, size_t{9}));

  // Nothing measurable: done, nothing shown.
  ViewportSearch blind(5);
  for (int round = 0; round < 10 && !blind.done(); ++round) {
    for (size_t index : blind.NextProbes()) {
      blind.Report(index, ViewportSearch::Place::kUnknown);
    }
  }
  EXPECT_TRUE(blind.done());
  EXPECT_TRUE(blind.empty());
}

TEST(KoanCliBatchTest, DiffAndItsBaseline) {
  const base::DictValue before_tree = Dict(kPageTree);
  Baseline before;
  before.document = "doc-1";
  before.kind = BaselineKind({.interactive = true});
  before.lines = BuildSnapshot(before_tree, {.interactive = true}).lines;

  // The email fixed, the alert gone, a "Saved" status new.
  std::string json = kPageTree;
  json.replace(json.find("\"ada@\""), 6, "\"ada@x.test\"");
  json.replace(
      json.find(
          R"("properties":[{"name":"invalid","value":{"type":"token","value":"true"}}],)"),
      std::string(
          R"("properties":[{"name":"invalid","value":{"type":"token","value":"true"}}],)")
          .size(),
      "");
  json.replace(json.find("\"Enter a valid email\""), 21, "\"Saved\"");
  json.replace(json.find("\"role\":{\"value\":\"alert\"}"), 24,
               "\"role\":{\"value\":\"status\"}");
  json.replace(json.find("\"backendDOMNodeId\":81"), 21,
               "\"backendDOMNodeId\":82");
  Baseline now;
  now.document = "doc-1";
  now.kind = before.kind;
  now.lines = BuildSnapshot(Dict(json), {.interactive = true}).lines;

  EXPECT_EQ(DiffText(before, now, "FULL\n"),
            "diff since the last snapshot of this page: 1 added, 1 changed, "
            "1 removed\n"
            "~ textbox \"Email\" = \"ada@x.test\" @12\n"
            "+ \"Saved\"\n"
            "- \"Enter a valid email\"\n");
  EXPECT_EQ(DiffText(now, now, "FULL\n"),
            "diff: no change since the last snapshot of this page\n");

  // Nothing to compare with: the whole snapshot, and why.
  EXPECT_EQ(DiffText(std::nullopt, now, "FULL\n"),
            "baseline reset (no earlier snapshot of this tab); the whole "
            "snapshot:\nFULL\n");
  Baseline navigated = now;
  navigated.document = "doc-2";
  EXPECT_NE(DiffText(before, navigated, "FULL\n")
                .find("baseline reset (the tab shows a new page)"),
            std::string::npos);
  Baseline compact = now;
  compact.kind = BaselineKind({});
  EXPECT_NE(DiffText(before, compact, "FULL\n")
                .find("the last snapshot was interactive, this one is compact"),
            std::string::npos);
  EXPECT_EQ(BaselineKind({.root = 11}), "compact of @11");

  // What the holder keeps: one baseline a tab, swapped.
  Baselines book;
  EXPECT_FALSE(book.Swap("T1", before));
  std::optional<Baseline> was = book.Swap("T1", now);
  ASSERT_TRUE(was);
  EXPECT_EQ(was->lines.size(), before.lines.size());
  EXPECT_FALSE(book.Swap("T2", now));
  // Over the wire to the holder and back.
  std::optional<Baseline> back = BaselineFromValue(BaselineToValue(now));
  ASSERT_TRUE(back);
  EXPECT_EQ(back->document, "doc-1");
  EXPECT_EQ(back->kind, now.kind);
  ASSERT_EQ(back->lines.size(), now.lines.size());
  EXPECT_EQ(back->lines[2].node, now.lines[2].node);
  EXPECT_EQ(back->lines[2].text, now.lines[2].text);
  EXPECT_FALSE(BaselineFromValue(Dict(R"({"document":"d","kind":"k",
                                          "lines":[[1]]})")));
}

// --- The guide
// ---------------------------------------------------------------------

TEST(KoanCliBatchTest, TheGuideEndsWithItsVersionAndWarnsOfAMismatch) {
  const std::string end = "[koan:skill] end of guide v" +
                          base::NumberToString(agent::kAgentInterfaceVersion) +
                          "\n";
  const std::string guide = SkillGuide("");
  ASSERT_GE(guide.size(), end.size());
  EXPECT_EQ(guide.substr(guide.size() - end.size()), end);
  for (const char* needed : {
           "snapshot <tab> --interactive",
           "--until url~/step-2",
           "--diff",
           "Verify by reading the result",
           "Never retry blindly",
           "A familiar URL does not tell you whose account",
           "koan help act",
           "loc:role=button[name=\"Continue\"]",
           "Koan.paused",
       }) {
    EXPECT_NE(guide.find(needed), std::string::npos) << needed;
  }

  Discovery same;
  same.agent_version = agent::kAgentInterfaceVersion;
  EXPECT_EQ(VersionNotice(same, agent::kAgentInterfaceVersion), "");
  EXPECT_EQ(VersionNotice(std::nullopt, agent::kAgentInterfaceVersion), "");
  Discovery old;
  old.version = "158.0.1.2";
  const std::string notice = VersionNotice(old, 2);
  EXPECT_EQ(notice.rfind("[koan:notice] ", 0), 0u);
  EXPECT_NE(notice.find("158.0.1.2"), std::string::npos);
  EXPECT_NE(notice.find("an older one"), std::string::npos);
  // The notice comes first, before anything an agent would act on.
  EXPECT_EQ(SkillGuide(notice).rfind(notice, 0), 0u);
  EXPECT_EQ(ParseDiscovery(R"({"socket":"/s","pid":2,"agentVersion":3})")
                ->agent_version,
            3);
}

TEST(KoanCliBatchTest, HelpForEachCommand) {
  EXPECT_EQ(Parse({"help", "act"}).help_topic, "act");
  EXPECT_EQ(Parse({"act", "--help"}).help_topic, "act");
  const std::optional<std::string> act = HelpText("act");
  ASSERT_TRUE(act);
  for (const char* flag : {"--until", "--snapshot", "--interactive", "--diff",
                           "--scope", "--locators", "--timeout", "--steps",
                           "--json", "loc:role=", "at most 50"}) {
    EXPECT_NE(act->find(flag), std::string::npos) << flag;
  }
  const std::optional<std::string> snapshot = HelpText("snapshot");
  ASSERT_TRUE(snapshot);
  for (const char* flag :
       {"--interactive", "--scope auto", "--scope viewport", "--scope full",
        "--scope @ref", "--diff", "--locators", "--full"}) {
    EXPECT_NE(snapshot->find(flag), std::string::npos) << flag;
  }
  EXPECT_TRUE(HelpText("hand-back"));
  EXPECT_FALSE(HelpText("frobnicate"));
}

}  // namespace
}  // namespace koan::cli
