// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_batch.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_snapshot.h"

namespace koan::cli {

namespace {

// Candidates a failed locator lists.
constexpr size_t kMaxCandidates = 10;

std::string_view VerbOf(ActKind act) {
  switch (act) {
    case ActKind::kClick:
      return "click";
    case ActKind::kFill:
      return "fill";
    case ActKind::kSelect:
      return "select";
    case ActKind::kChoose:
      return "choose";
    case ActKind::kPress:
      return "press";
    case ActKind::kScroll:
      return "scroll";
    case ActKind::kNavigate:
      return "navigate";
    case ActKind::kAutofill:
      return "autofill";
  }
  return "";
}

std::optional<ActKind> ActOf(std::string_view verb) {
  for (ActKind act : {ActKind::kClick, ActKind::kFill, ActKind::kSelect,
                      ActKind::kChoose, ActKind::kPress, ActKind::kScroll,
                      ActKind::kNavigate, ActKind::kAutofill}) {
    if (VerbOf(act) == verb) {
      return act;
    }
  }
  return std::nullopt;
}

bool IsTarget(std::string_view word) {
  std::string ignored;
  return ParseRef(word) ||
         (word.starts_with("loc:") && ParseLocator(word, &ignored));
}

// `fill @12=Ada`: the ref and the text, if `word` is one.
std::optional<std::pair<std::string, std::string>> RefAssignment(
    std::string_view word) {
  const size_t equals = word.find('=');
  if (equals == std::string_view::npos || !ParseRef(word.substr(0, equals))) {
    return std::nullopt;
  }
  return std::make_pair(std::string(word.substr(0, equals)),
                        std::string(word.substr(equals + 1)));
}

std::string Quote(std::string_view text) {
  std::string out = "\"";
  for (char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out + "\"";
}

std::string Usage(ActKind act) {
  switch (act) {
    case ActKind::kClick:
      return "click @ref";
    case ActKind::kFill:
      return "fill @ref <text> (or fill @ref=<text> @ref=<text> ...)";
    case ActKind::kSelect:
    case ActKind::kChoose:
      return std::string(VerbOf(act)) + " @ref <option text>";
    case ActKind::kPress:
      return "press <key>";
    case ActKind::kScroll:
      return "scroll <pixels>";
    case ActKind::kNavigate:
      return "navigate <url>";
    case ActKind::kAutofill:
      return "autofill @ref <suggestion number or label>";
  }
  return "";
}

// The AX value of `key` as text (a string; anything else is "").
std::string AxText(const base::DictValue& node, std::string_view key) {
  const std::string* value =
      node.FindStringByDottedPath(std::string(key) + ".value");
  return value ? *value : std::string();
}

std::string Candidate(const base::DictValue& node) {
  std::string line = AxText(node, "role");
  const std::string name = CollapseSpace(AxText(node, "name"));
  if (!name.empty()) {
    line += " " + Quote(name);
  }
  if (std::optional<int> id = node.FindInt("backendDOMNodeId")) {
    line += " @" + base::NumberToString(*id);
  }
  return line;
}

}  // namespace

// --- Locators
// -----------------------------------------------------------------

std::optional<Locator> ParseLocator(std::string_view text, std::string* error) {
  constexpr std::string_view kForm =
      "a locator is loc:role=<role> or loc:role=<role>[name=\"<name>\"]";
  if (!text.starts_with("loc:role=")) {
    *error = std::string(kForm);
    return std::nullopt;
  }
  std::string_view rest = text.substr(9);
  Locator locator;
  size_t i = 0;
  while (i < rest.size() && base::IsAsciiAlphaNumeric(rest[i])) {
    ++i;
  }
  locator.role = std::string(rest.substr(0, i));
  rest = rest.substr(i);
  if (locator.role.empty()) {
    *error = std::string(kForm);
    return std::nullopt;
  }
  if (rest.empty()) {
    return locator;
  }
  if (!rest.starts_with("[name=\"")) {
    *error = std::string(kForm);
    return std::nullopt;
  }
  rest = rest.substr(7);
  std::string name;
  size_t at = 0;
  for (; at < rest.size() && rest[at] != '"'; ++at) {
    if (rest[at] == '\\' && at + 1 < rest.size()) {
      ++at;
    }
    name += rest[at];
  }
  if (rest.substr(std::min(at, rest.size())) != "\"]") {
    *error = std::string(kForm) + " (the name in double quotes, \\\" inside)";
    return std::nullopt;
  }
  locator.name = std::move(name);
  return locator;
}

std::string LocatorText(std::string_view role, std::string_view name) {
  return "loc:role=" + std::string(role) + "[name=" + Quote(name) + "]";
}

LocatorMatch MatchLocator(const base::DictValue& ax_tree,
                          const Locator& locator,
                          std::string_view given) {
  LocatorMatch match;
  std::vector<const base::DictValue*> found;
  std::vector<const base::DictValue*> same_role;
  const std::string wanted =
      locator.name ? CollapseSpace(*locator.name) : std::string();
  if (const base::ListValue* nodes = ax_tree.FindList("nodes")) {
    for (const base::Value& value : *nodes) {
      const base::DictValue* node = value.GetIfDict();
      if (!node || node->FindBool("ignored").value_or(false) ||
          !node->FindInt("backendDOMNodeId") ||
          AxText(*node, "role") != locator.role) {
        continue;
      }
      same_role.push_back(node);
      if (!locator.name || CollapseSpace(AxText(*node, "name")) == wanted) {
        found.push_back(node);
      }
    }
  }
  if (found.size() == 1) {
    match.node = found[0]->FindInt("backendDOMNodeId");
    return match;
  }
  const std::vector<const base::DictValue*>& listed =
      found.empty() ? same_role : found;
  std::vector<std::string> candidates;
  for (const base::DictValue* node : listed) {
    if (candidates.size() == kMaxCandidates) {
      candidates.push_back("...");
      break;
    }
    candidates.push_back(Candidate(*node));
  }
  const std::string what =
      locator.role + (locator.name ? " named " + Quote(wanted) : "");
  if (!found.empty()) {
    match.error = "Koan.ambiguous: " + std::string(given) + " matches " +
                  base::NumberToString(found.size()) + " elements (" +
                  base::JoinString(candidates, ", ") +
                  "); use one's @ref from a snapshot";
  } else if (!same_role.empty()) {
    match.error = "Koan.notFound: no " + what + " on the page; the " +
                  locator.role +
                  " elements: " + base::JoinString(candidates, ", ");
  } else {
    match.error = "Koan.notFound: no " + what +
                  " on the page (and no element with role " + locator.role +
                  "); take a snapshot";
  }
  return match;
}

// --- Steps
// --------------------------------------------------------------------

bool IsStepVerb(std::string_view word) {
  return ActOf(word).has_value();
}

bool ParseSteps(const std::vector<std::string>& words,
                size_t* at,
                std::vector<Step>* steps,
                std::string* error) {
  size_t i = *at;
  auto add = [&](Step step) {
    if (steps->size() >= kMaxSteps) {
      *error = "a batch takes at most " + base::NumberToString(kMaxSteps) +
               " steps; split it into several koan act commands";
      return false;
    }
    steps->push_back(std::move(step));
    return true;
  };
  while (i < words.size() && !words[i].starts_with("--")) {
    const std::optional<ActKind> act = ActOf(words[i]);
    if (!act) {
      *error = "unknown action \"" + words[i] +
               "\" (click, fill, select, choose, press, scroll, navigate, "
               "autofill)";
      return false;
    }
    ++i;
    const size_t left = words.size() - i;
    Step step;
    step.act = *act;
    auto usage = [&] {
      *error = "usage: " + Usage(*act);
      return false;
    };
    switch (*act) {
      case ActKind::kFill:
        if (left >= 1 && RefAssignment(words[i])) {
          while (i < words.size()) {
            std::optional<std::pair<std::string, std::string>> assignment =
                RefAssignment(words[i]);
            if (!assignment) {
              break;
            }
            Step fill;
            fill.act = ActKind::kFill;
            fill.target = std::move(assignment->first);
            fill.text = std::move(assignment->second);
            if (!add(std::move(fill))) {
              return false;
            }
            ++i;
          }
          continue;
        }
        [[fallthrough]];
      case ActKind::kSelect:
      case ActKind::kChoose:
      case ActKind::kAutofill:
        if (left < 2 || !IsTarget(words[i]) ||
            (*act != ActKind::kFill && words[i + 1].empty())) {
          return usage();
        }
        step.target = words[i];
        step.text = words[i + 1];
        i += 2;
        break;
      case ActKind::kClick:
        if (left < 1 || !IsTarget(words[i])) {
          return usage();
        }
        step.target = words[i++];
        break;
      case ActKind::kPress:
      case ActKind::kNavigate:
        if (left < 1 || words[i].empty()) {
          return usage();
        }
        step.text = words[i++];
        break;
      case ActKind::kScroll:
        if (left < 1 || !base::StringToInt(words[i], &step.scroll_by) ||
            step.scroll_by == 0) {
          return usage();
        }
        ++i;
        break;
    }
    if (!add(std::move(step))) {
      return false;
    }
  }
  *at = i;
  return true;
}

bool ParseStepLines(std::string_view text,
                    std::vector<Step>* steps,
                    std::string* error) {
  int number = 0;
  for (std::string_view line : base::SplitStringPiece(
           text, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    ++number;
    const ShellLine words = ParseShellLine(line);
    if (words.empty) {
      continue;
    }
    const std::string where = "line " + base::NumberToString(number) + ": ";
    if (!words.error.empty() || words.json) {
      *error = where + (words.json ? "a steps file takes steps as koan act "
                                     "does, not JSON"
                                   : words.error);
      return false;
    }
    size_t at = 0;
    if (!ParseSteps(words.args, &at, steps, error)) {
      *error = where + *error;
      return false;
    }
    if (at != words.args.size()) {
      *error = where + "options go on the koan act command line";
      return false;
    }
  }
  if (steps->empty()) {
    *error = "the steps file has no steps";
    return false;
  }
  return true;
}

std::string StepLabel(const Step& step) {
  std::string label(VerbOf(step.act));
  switch (step.act) {
    case ActKind::kClick:
    case ActKind::kFill:
    case ActKind::kSelect:
    case ActKind::kChoose:
      return label + " " + step.target;
    case ActKind::kAutofill:
      // The pick is a label the user sees, never a value.
      return label + " " + step.target + " " + Quote(step.text);
    case ActKind::kPress:
    case ActKind::kNavigate:
      return label + " " + step.text;
    case ActKind::kScroll:
      return label + " " + base::NumberToString(step.scroll_by);
  }
  return label;
}

// --- --until
// --------------------------------------------------------------------

std::optional<Until> ParseUntil(std::string_view text, std::string* error) {
  Until until;
  if (text.starts_with("url~") || text.starts_with("text~")) {
    until.kind = text.starts_with("url~") ? Until::kUrl : Until::kText;
    until.needle = std::string(text.substr(text.find('~') + 1));
  } else if (text.ends_with(" gone") &&
             ParseRef(text.substr(0, text.size() - 5))) {
    until.kind = Until::kRefGone;
    until.ref = std::string(text.substr(0, text.size() - 5));
  } else if (const size_t tilde = text.find('~');
             tilde != std::string_view::npos &&
             ParseRef(text.substr(0, tilde))) {
    until.kind = Until::kRefText;
    until.ref = std::string(text.substr(0, tilde));
    until.needle = std::string(text.substr(tilde + 1));
  } else {
    *error = "--until takes url~TEXT, text~TEXT, @ref~TEXT or \"@ref gone\"";
    return std::nullopt;
  }
  if (until.kind != Until::kRefGone && until.needle.empty()) {
    *error = "--until " + std::string(text) + ": nothing to wait for after ~";
    return std::nullopt;
  }
  return until;
}

std::string UntilText(const Until& until) {
  switch (until.kind) {
    case Until::kUrl:
      return "url~" + until.needle;
    case Until::kText:
      return "text~" + until.needle;
    case Until::kRefText:
      return until.ref + "~" + until.needle;
    case Until::kRefGone:
      return until.ref + " gone";
  }
  return "";
}

bool UrlMatches(std::string_view url, const Until& until) {
  return until.kind == Until::kUrl &&
         url.find(until.needle) != std::string_view::npos;
}

std::string PageTextExpression(std::string_view needle) {
  const std::string quoted =
      base::WriteJson(base::Value(CollapseSpace(needle))).value_or("\"\"");
  return "(() => ((document.body && document.body.innerText) || '')"
         ".replace(/\\s+/g, ' ').includes(" +
         quoted + "))()";
}

const char kElementTextFunction[] = R"(function(needle) {
  const el = this.nodeType === 1 ? this : this.parentElement;
  const connected = this.isConnected;
  const shown = !!el && connected && el.getClientRects().length > 0;
  let text = '';
  if (el && typeof el.value === 'string') text += el.value + ' ';
  if (el) text += el.innerText || el.textContent || '';
  return {connected, shown,
          has: !!needle && text.replace(/\s+/g, ' ').includes(needle)};
})";

bool ElementMeets(const Until& until, const base::Value* returned, bool gone) {
  const base::DictValue* dict = returned ? returned->GetIfDict() : nullptr;
  if (until.kind == Until::kRefGone) {
    return gone || !dict || !dict->FindBool("connected").value_or(false) ||
           !dict->FindBool("shown").value_or(false);
  }
  return until.kind == Until::kRefText && !gone && dict &&
         dict->FindBool("has").value_or(false);
}

// --- BatchGate
// ------------------------------------------------------------------

std::string PausedLine(std::string_view tab,
                       std::string_view reason,
                       std::string_view sticky) {
  const std::string wait = "koan wait " + std::string(tab) + " --resumed";
  if (sticky.empty() && reason == "user-input") {
    return "Koan.paused: the user is using this tab (user-input); run `" +
           wait +
           "`: it returns once they stop, with a fresh snapshot of the "
           "page as they left it. Go on from that snapshot, not from your "
           "old plan";
  }
  const std::string why =
      sticky.empty() ? std::string(reason) : std::string(sticky);
  return "Koan.paused: the user is handling this tab (" + why +
         "); it waits for them to hand it back. Tell the user what you "
         "need and wait; do not work around it";
}

BatchGate::BatchGate(std::string target) : target_(std::move(target)) {}
BatchGate::~BatchGate() = default;

void BatchGate::OnEvent(const Message& event) {
  const std::string* target = event.params.FindString("targetId");
  if (!target || *target != target_) {
    return;
  }
  const std::string* reason = event.params.FindString("reason");
  const std::string why = reason ? *reason : std::string();
  if (event.method == "Koan.paused") {
    paused_ = true;
    paused_reason_ = why;
    const std::string* sticky = event.params.FindString("sticky");
    paused_sticky_ = sticky ? *sticky : std::string();
  } else if (event.method == "Koan.resumed") {
    paused_ = false;
    paused_sticky_.clear();
  } else if (event.method == "Koan.userNeeded") {
    needs_.insert(why);
  } else if (event.method == "Koan.userNeededDone") {
    needs_.erase(why);
  } else if (event.method == "Target.detachedFromTarget" ||
             event.method == "Target.targetDestroyed") {
    gone_ = true;
  }
}

void BatchGate::Replay(const base::ListValue& events) {
  for (const base::Value& event : events) {
    const std::string* json = event.GetIfString();
    std::optional<Message> message = json ? ParseMessage(*json) : std::nullopt;
    // An interstitial's Koan.userNeeded has no Done event (the browser
    // refuses input to it itself, Koan.interstitial): an old one is not
    // taken for one that still waits.
    const std::string* reason =
        message ? message->params.FindString("reason") : nullptr;
    if (message && message->method.starts_with("Koan.") &&
        !(reason && *reason == "interstitial")) {
      OnEvent(*message);
    }
  }
}

void BatchGate::OnTab(const base::DictValue* tab) {
  if (!tab) {
    gone_ = true;
    return;
  }
  const std::string* state = tab->FindString("state");
  const bool paused = state && *state == "paused";
  if (paused && !paused_) {
    paused_reason_ = "user";
  }
  // How the pause stands now (Koan.listTabs "pause").
  if (const base::DictValue* pause = tab->FindDict("pause"); paused && pause) {
    if (const std::string* reason = pause->FindString("pauseReason")) {
      paused_reason_ = *reason;
    }
    const std::string* sticky = pause->FindString("sticky");
    paused_sticky_ = sticky ? *sticky : std::string();
  }
  paused_ = paused;
}

std::optional<std::string> BatchGate::StopReason() const {
  if (gone_) {
    return "Koan.tabGone: the tab closed";
  }
  if (paused_) {
    return PausedLine(target_, paused_reason_, paused_sticky_);
  }
  if (!needs_.empty()) {
    std::vector<std::string> reasons(needs_.begin(), needs_.end());
    return "Koan.userNeeded: the page waits for the user (" +
           base::JoinString(reasons, ", ") + "); tell the user what it asks";
  }
  return std::nullopt;
}

// --- ResumeWaiter
// ---------------------------------------------------------------

ResumeWaiter::ResumeWaiter(std::string target) : target_(std::move(target)) {}
ResumeWaiter::~ResumeWaiter() = default;

void ResumeWaiter::Paused(std::string reason, std::string sticky) {
  // Only a pause from the user's input ends by itself.
  if (!sticky.empty() || reason != "user-input") {
    outcome_ = Outcome::kSticky;
    why_ = sticky.empty() ? std::move(reason) : std::move(sticky);
  }
}

void ResumeWaiter::OnTab(const base::DictValue* tab) {
  if (done()) {
    return;
  }
  if (!tab) {
    outcome_ = Outcome::kTabGone;
    return;
  }
  const std::string* state = tab->FindString("state");
  if (!state || *state != "paused") {
    outcome_ = Outcome::kNotPaused;
    return;
  }
  const base::DictValue* pause = tab->FindDict("pause");
  const std::string* reason =
      pause ? pause->FindString("pauseReason") : nullptr;
  const std::string* sticky = pause ? pause->FindString("sticky") : nullptr;
  // A browser older than auto-resume says nothing more: its pauses wait for
  // Hand back.
  Paused(reason ? *reason : "user", sticky ? *sticky : std::string());
}

void ResumeWaiter::OnEvent(const Message& event) {
  if (done()) {
    return;
  }
  const std::string* target = event.params.FindString("targetId");
  if (!target || *target != target_) {
    return;
  }
  if (event.method == "Koan.resumed") {
    outcome_ = Outcome::kResumed;
    resumed_ = event.params.Clone();
  } else if (event.method == "Koan.paused") {
    const std::string* reason = event.params.FindString("reason");
    const std::string* sticky = event.params.FindString("sticky");
    Paused(reason ? *reason : std::string(), sticky ? *sticky : std::string());
  } else if (event.method == "Koan.released") {
    outcome_ = Outcome::kTakenBack;
  } else if (event.method == "Target.targetDestroyed" ||
             event.method == "Target.detachedFromTarget") {
    outcome_ = Outcome::kTabGone;
  }
}

void ResumeWaiter::OnClosed() {
  if (!done()) {
    outcome_ = Outcome::kStopped;
  }
}

void ResumeWaiter::OnTimeout() {
  if (!done()) {
    outcome_ = Outcome::kTimedOut;
  }
}

std::string ResumeWaiter::Line(std::string_view tab,
                               int timeout_seconds) const {
  switch (outcome_) {
    case Outcome::kResumed: {
      const std::string* reason = resumed_.FindString("reason");
      const double ms = resumed_.FindDouble("pausedMs").value_or(0);
      std::string line =
          "resumed: " + (reason ? *reason : std::string()) + " after " +
          base::NumberToString(static_cast<int>(ms / 1000)) + " s";
      if (resumed_.FindBool("navigated").value_or(false)) {
        line += "; the user navigated";
      } else if (resumed_.FindBool("changed").value_or(false)) {
        line += "; the page changed";
      } else {
        line += "; the page did not change";
      }
      if (const std::string* url = resumed_.FindString("url")) {
        line += " (" + *url + ")";
      }
      return line;
    }
    case Outcome::kNotPaused:
      return "not paused";
    case Outcome::kSticky:
      return "Koan.paused: the user is handling this tab (" + why_ +
             "): it will not come back by itself. Tell the user what you "
             "need, and ask them to hand it back when they are done";
    case Outcome::kTakenBack:
      return "Koan.released: the user took this tab back; it is no longer "
             "yours";
    case Outcome::kTabGone:
      return "Koan.tabGone: the tab closed";
    case Outcome::kStopped:
      return "Koan.notConnected: the connection to Koan closed (the user "
             "stopped the agents, or Koan quit)";
    case Outcome::kTimedOut:
      return "Koan.timeout: " + std::string(tab) + " was still paused after " +
             base::NumberToString(timeout_seconds) +
             " s; ask the user, or wait again";
    case Outcome::kWaiting:
      break;
  }
  return std::string();
}

// --- What it prints
// -------------------------------------------------------------

std::string ReceiptLine(size_t index,
                        size_t count,
                        const Step& step,
                        std::string_view outcome) {
  return base::NumberToString(index + 1) + "/" + base::NumberToString(count) +
         " " + StepLabel(step) + ": " + std::string(outcome);
}

std::string StoppedLine(size_t index,
                        size_t count,
                        const Step& step,
                        std::string_view why) {
  return "stopped at " + ReceiptLine(index, count, step, why);
}

std::string NotRunLine(const std::vector<Step>& steps, size_t stopped) {
  std::vector<std::string> left;
  for (size_t i = stopped + 1; i < steps.size(); ++i) {
    left.push_back(base::NumberToString(i + 1) + "/" +
                   base::NumberToString(steps.size()) + " " +
                   StepLabel(steps[i]));
  }
  return left.empty() ? std::string()
                      : "not run: " + base::JoinString(left, ", ");
}

// --- ViewportSearch
// ---------------------------------------------------------------

ViewportSearch::ViewportSearch(size_t count, size_t per_round)
    : count_(static_cast<long>(count)),
      per_round_(std::max<size_t>(per_round, 1)),
      places_(count) {
  first_.hi = count_;
  last_.hi = count_;
  first_.done = last_.done = count == 0;
}

ViewportSearch::~ViewportSearch() = default;

bool ViewportSearch::empty() const {
  return count_ == 0 || last_.lo < 0 || first_.hi >= count_ ||
         last_.lo < first_.hi;
}

std::vector<size_t> ViewportSearch::ProbesIn(const Bound& bound) const {
  std::vector<size_t> probes;
  if (bound.done) {
    return probes;
  }
  const long span = bound.hi - bound.lo;
  for (size_t t = 1; t <= per_round_; ++t) {
    const long index = bound.lo + span * static_cast<long>(t) /
                                      static_cast<long>(per_round_ + 1);
    if (index <= bound.lo || index >= bound.hi || places_[index]) {
      continue;
    }
    if (probes.empty() || probes.back() != static_cast<size_t>(index)) {
      probes.push_back(static_cast<size_t>(index));
    }
  }
  // Too few lines left between for evenly spaced ones: every one not yet
  // measured.
  if (probes.empty()) {
    for (long index = bound.lo + 1; index < bound.hi; ++index) {
      if (!places_[index]) {
        probes.push_back(static_cast<size_t>(index));
        if (probes.size() == per_round_) {
          break;
        }
      }
    }
  }
  return probes;
}

std::vector<size_t> ViewportSearch::NextProbes() {
  Narrow();
  std::vector<size_t> probes = ProbesIn(first_);
  for (size_t index : ProbesIn(last_)) {
    if (std::find(probes.begin(), probes.end(), index) == probes.end()) {
      probes.push_back(index);
    }
  }
  std::sort(probes.begin(), probes.end());
  if (probes.empty()) {
    first_.done = last_.done = true;
  }
  return probes;
}

void ViewportSearch::Report(size_t index, Place place) {
  if (index < places_.size()) {
    places_[index] = place;
  }
  Narrow();
}

void ViewportSearch::Narrow() {
  // The first line shown: after the last line above (a fixed header before
  // it is passed over), the first line that is not above.
  if (!first_.done) {
    for (long i = first_.lo + 1; i < first_.hi; ++i) {
      if (places_[i] == Place::kAbove) {
        first_.lo = i;
      }
    }
    for (long i = first_.lo + 1; i < first_.hi; ++i) {
      if (places_[i] == Place::kIn || places_[i] == Place::kBelow) {
        first_.hi = i;
        break;
      }
    }
    first_.done = first_.hi - first_.lo <= 1;
  }
  // The last line shown: before the first line below (a fixed footer after
  // it is passed over), the last line that is not below.
  if (!last_.done) {
    for (long i = last_.lo + 1; i < last_.hi; ++i) {
      if (places_[i] == Place::kBelow) {
        last_.hi = i;
        break;
      }
    }
    for (long i = last_.hi - 1; i > last_.lo; --i) {
      if (places_[i] == Place::kIn || places_[i] == Place::kAbove) {
        last_.lo = i;
        break;
      }
    }
    last_.done = last_.hi - last_.lo <= 1;
  }
}

}  // namespace koan::cli
