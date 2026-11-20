// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_BATCH_H_
#define KOAN_CLI_KOAN_CLI_BATCH_H_

#include <stddef.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "koan/cli/koan_cli_protocol.h"

// Several steps in one `koan act` (.agent/notes/agent-control.md section 12,
// "Batches"): the steps, what they act on (a ref or a locator), what the
// batch waits for at the end (--until), when it must stop (the user took the
// tab, the page waits for them), and what it prints for each step. Pure: the
// steps run in koan_cli_main.cc, one after another over the one connection,
// each through the browser's method policy like a single act.
namespace koan::cli {

// What a step does.
enum class ActKind {
  kClick,
  kFill,
  // Chooses an option of a dropdown (<select>), by its label or value.
  kSelect,
  // Chooses an option of any dropdown: a <select> as kSelect does, a custom
  // one (an ARIA combobox, a button with a listbox) by opening it and
  // clicking the option, by its text.
  kChoose,
  kPress,
  kScroll,
  kNavigate,
  // Picks one of Chrome's autofill suggestions for a field (`koan autofill`):
  // its number, or its label.
  kAutofill,
};

// The most steps one batch takes: enough for a long form, few enough that a
// batch that went wrong stops soon (it stops at the first failure anyway).
inline constexpr size_t kMaxSteps = 50;

// A step's target by what the user sees, not by a node id:
//   loc:role=button[name="Continue"]
// It must name exactly one node of the page's accessibility tree (its role,
// and its name with whitespace collapsed, exactly), or the step fails and
// lists the candidates. No name: the only node of that role.
struct Locator {
  std::string role;
  std::optional<std::string> name;
};
// "loc:..." as above; nullopt (and `error`) for anything else.
std::optional<Locator> ParseLocator(std::string_view text, std::string* error);
// The locator as `act` takes it.
std::string LocatorText(std::string_view role, std::string_view name);

struct Step {
  ActKind act = ActKind::kClick;
  // click, fill, select, choose: "@12" or "loc:...".
  std::string target;
  // fill: the text; select, choose: the option; press: the key; navigate:
  // the URL; autofill: the suggestion (its number or its label).
  std::string text;
  // scroll: pixels down.
  int scroll_by = 0;
};

// Whether `word` starts a step (click, fill, ...).
bool IsStepVerb(std::string_view word);

// Reads steps from `words`, starting at `*at`, until a word that begins
// with "--" where a step would start, or the end; `*at` is left there.
// Each verb takes its words by count (fill @ref <text>), so a text that
// looks like an option ("--snapshot") is the text. `fill @12=Ada @13=Love`
// is two fills (only @refs: a locator has "=" of its own). Fails (`error`)
// on a malformed step, or past kMaxSteps in all (`steps` may hold some).
bool ParseSteps(const std::vector<std::string>& words,
                size_t* at,
                std::vector<Step>* steps,
                std::string* error);

// A steps file (`koan act <tab> --steps FILE`): one or more steps per line,
// quoted as a shell would ('#' starts a comment line). The error names the
// line.
bool ParseStepLines(std::string_view text,
                    std::vector<Step>* steps,
                    std::string* error);

// "fill @12", "click loc:role=button[name=\"Next\"]", "press Enter": the
// step, as its receipt names it (never fill's text: a password).
std::string StepLabel(const Step& step);

// --- --until -----------------------------------------------------------------

struct Until {
  enum Kind {
    // url~TEXT: the tab's URL contains TEXT.
    kUrl,
    // text~TEXT: the page's text contains TEXT.
    kText,
    // @ref~TEXT: that element's text (or a field's value) contains TEXT.
    kRefText,
    // "@ref gone": that element is no longer on the page, or not shown.
    kRefGone,
  };
  Kind kind = kUrl;
  std::string needle;
  std::string ref;
};
std::optional<Until> ParseUntil(std::string_view text, std::string* error);
// "url~/step-2", as given.
std::string UntilText(const Until& until);
bool UrlMatches(std::string_view url, const Until& until);
// For Runtime.evaluate (returnByValue): whether the page's text holds
// `needle` (JSON-quoted into the expression).
std::string PageTextExpression(std::string_view needle);
// For Runtime.callFunctionOn on the ref's node, function(needle):
// {connected, shown, has}.
extern const char kElementTextFunction[];
// Whether the element's answer meets the condition (`gone`: the node could
// not be resolved, or the tab shows another document).
bool ElementMeets(const Until& until, const base::Value* returned, bool gone);

// --- When a batch must stop --------------------------------------------------

// What to tell an agent whose tab the user paused (Koan.paused {reason,
// sticky}; design section 9.2, auto-resume): a pause from the user's input
// that may end by itself says to run `koan wait <tab> --resumed` and go on
// from the snapshot it prints; one that waits for Hand back (Take over, Keep
// paused, a password field, the page asking: `sticky`) says the user is
// handling the tab. `tab` is the tab as the agent names it.
std::string PausedLine(std::string_view tab,
                       std::string_view reason,
                       std::string_view sticky);

// Whether the user has the tab or the page waits for them, from what the
// browser said: the tab's state in Koan.listTabs ("paused"), the client's
// Koan.* events (paused, resumed, userNeeded, userNeededDone, the tab
// closing). Checked before every step; the browser refuses every input to a
// paused tab anyway (TargetRegistry::MaySend()), so this saves the doomed
// step and says why.
class BatchGate {
 public:
  explicit BatchGate(std::string target);
  BatchGate(const BatchGate&) = delete;
  BatchGate& operator=(const BatchGate&) = delete;
  ~BatchGate();

  void OnEvent(const Message& event);
  // The Koan.* events the holder kept (Hold.status), oldest first: what
  // waits for the user from before this batch (not an interstitial, whose
  // Koan.userNeeded never ends with a Done).
  void Replay(const base::ListValue& events);
  // The tab's entry in Koan.listTabs, or nullptr if it is not listed.
  void OnTab(const base::DictValue* tab);

  // Why the batch must stop now (an error line), or nullopt.
  std::optional<std::string> StopReason() const;

 private:
  const std::string target_;
  bool paused_ = false;
  std::string paused_reason_;
  // Why the pause waits for Hand back, if it does (Koan.paused "sticky").
  std::string paused_sticky_;
  bool gone_ = false;
  std::set<std::string> needs_;
};

// --- koan wait <tab> --resumed ---------------------------------------------

// What `koan wait <tab> --resumed` makes of what the browser says, until the
// tab comes back to the agent (Koan.resumed: the user stopped using it, or
// handed it back) or will not by itself: a pause the user is handling
// (sticky: Take over, Keep paused, a password field, the page asking), the
// user taking the tab back, the tab closing, the connection ending (Stop all
// agents, a revocation), or the timeout.
class ResumeWaiter {
 public:
  enum class Outcome {
    kWaiting,
    kResumed,
    // Not paused when asked: nothing to wait for.
    kNotPaused,
    kSticky,
    kTakenBack,
    kTabGone,
    kStopped,
    kTimedOut,
  };

  explicit ResumeWaiter(std::string target);
  ResumeWaiter(const ResumeWaiter&) = delete;
  ResumeWaiter& operator=(const ResumeWaiter&) = delete;
  ~ResumeWaiter();

  // The tab's entry in Koan.listTabs when the wait starts (nullptr: not
  // listed).
  void OnTab(const base::DictValue* tab);
  void OnEvent(const Message& event);
  void OnClosed();
  void OnTimeout();

  Outcome outcome() const { return outcome_; }
  bool done() const { return outcome_ != Outcome::kWaiting; }
  // Exit 0 (and the snapshot) for these.
  bool ok() const {
    return outcome_ == Outcome::kResumed || outcome_ == Outcome::kNotPaused;
  }
  // Koan.resumed's params, once resumed.
  const base::DictValue& resumed() const { return resumed_; }
  // The line koan prints: "resumed: user-idle after 6 s; the page changed"
  // for kResumed, the error and what to do for the rest. `tab` is the tab as
  // the agent named it; `timeout` the wait's.
  std::string Line(std::string_view tab, int timeout_seconds) const;

 private:
  void Paused(std::string reason, std::string sticky);

  const std::string target_;
  Outcome outcome_ = Outcome::kWaiting;
  std::string why_;
  base::DictValue resumed_;
};

// --- What it prints ----------------------------------------------------------

// "2/4 fill @13: ok".
std::string ReceiptLine(size_t index,
                        size_t count,
                        const Step& step,
                        std::string_view outcome);
// "stopped at 3/4 choose @20: Koan.paused: ...".
std::string StoppedLine(size_t index,
                        size_t count,
                        const Step& step,
                        std::string_view why);
// "not run: 4/4 click @31, ..." for the steps after `stopped`, or "".
std::string NotRunLine(const std::vector<Step>& steps, size_t stopped);

// --- Locators ----------------------------------------------------------------

struct LocatorMatch {
  // The one node's backend DOM node id.
  std::optional<int> node;
  // No node: the error, listing the candidates.
  std::string error;
};
// The node of `ax_tree` (Accessibility.getFullAXTree) `locator` names.
LocatorMatch MatchLocator(const base::DictValue& ax_tree,
                          const Locator& locator,
                          std::string_view given);

// --- Scope: what the viewport shows -----------------------------------------

// Finds which of a snapshot's lines (in page order) the viewport shows,
// measuring few of them: k-ary searches, a round of probes at a time, for
// the first line whose top is in the viewport and the last one. Lines are
// in page order, and their tops grow with it in a page's normal flow; a
// fixed header at the start is passed over (the last probe above the
// viewport counts, not the first one in it).
class ViewportSearch {
 public:
  // `count` lines; up to `per_round` probes a round for each of the two
  // searches.
  explicit ViewportSearch(size_t count, size_t per_round = 8);
  ViewportSearch(const ViewportSearch&) = delete;
  ViewportSearch& operator=(const ViewportSearch&) = delete;
  ~ViewportSearch();

  // The lines to measure next; empty when done.
  std::vector<size_t> NextProbes();
  // Where line `index`'s top is: above the viewport, in it, below it;
  // kUnknown for a line with no box.
  enum class Place { kUnknown, kAbove, kIn, kBelow };
  void Report(size_t index, Place place);

  bool done() const { return first_.done && last_.done; }
  // The lines shown: [first(), last()], none if empty().
  bool empty() const;
  size_t first() const { return static_cast<size_t>(first_.hi); }
  size_t last() const { return static_cast<size_t>(last_.lo); }

 private:
  // A boundary between two kinds of lines, somewhere in (lo, hi).
  struct Bound {
    long lo = -1;
    long hi = 0;
    bool done = false;
  };
  void Narrow();
  std::vector<size_t> ProbesIn(const Bound& bound) const;

  const long count_;
  const size_t per_round_;
  // The first shown line: lo is above the viewport, hi is not.
  Bound first_;
  // The last shown line: lo is not below the viewport, hi is.
  Bound last_;
  std::vector<std::optional<Place>> places_;
};

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_BATCH_H_
