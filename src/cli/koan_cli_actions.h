// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_ACTIONS_H_
#define KOAN_CLI_KOAN_CLI_ACTIONS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

// What `koan act` runs on a ref's node, and how it reads the answers, as pure
// functions (.agent/notes/agent-control.md section 12). The calls themselves
// are in koan_cli_main.cc. Every action says what it did, or fails with a Koan
// error that says what to do instead: never "ok" for nothing.
namespace koan::cli {

// For Runtime.callFunctionOn on the node (returnByValue). They run in the
// page, as a script there could; they only read the node and, for select,
// choose an option and fire the events a user's choice fires.
//
// What fill may do with the node: {kind: "text", before, secret} for a text
// field (secret: a password field) or an editable element, {kind: "select"}
// for a <select>, else {kind: "notText" | "disabled" | "readonly", what}.
extern const char kFillTargetFunction[];
// Selects what the field holds, so that typing replaces it.
extern const char kSelectContentsFunction[];
// What the field holds now.
extern const char kReadTextFunction[];
// Chooses an option of a <select> (the node, or the <select> of an <option>
// node, whose argument is then ignored) by its label, else its value, else
// its label in any case; fires input and change when it changed. No popup is
// opened, so this works in a background tab.
extern const char kSelectOptionFunction[];

// The value a call returned, or nullptr if the page threw or it returned
// nothing.
const base::Value* ReturnedValue(const base::DictValue& call_result);

enum class FillKind { kText, kSelect, kRefused };
struct FillTarget {
  FillKind kind = FillKind::kRefused;
  // kText: what the field held, and whether it is a password field (whose
  // content is never printed).
  std::string before;
  bool secret = false;
  // kRefused: the error to print.
  std::string refusal;
};
FillTarget ReadFillTarget(const base::Value* returned, std::string_view ref);

enum class FillOutcome {
  kFilled,
  // The field holds something else than the text, but it changed (a page
  // that formats what is typed).
  kAltered,
  // Nothing changed and it does not hold the text: an error.
  kUnchanged,
};
FillOutcome CheckFill(std::string_view before,
                      std::string_view now,
                      std::string_view wanted);
// The line for an outcome: "ok", a note, or the error. `now` is what the
// field holds; a password field's (`secret`) is not shown.
std::string FillOutcomeText(FillOutcome outcome,
                            std::string_view ref,
                            std::string_view now,
                            bool secret);

struct SelectOutcome {
  bool ok = false;
  // ok: what to print ("ok: selected ..."); else the error.
  std::string text;
  // ok: the option chosen.
  std::string label;
  std::string value;
};
SelectOutcome ReadSelectOutcome(const base::Value* returned,
                                std::string_view ref);
// Whether a select's answer says the node is no <select> or <option> (a click
// on it then reports its own error).
bool IsNotSelect(const base::Value* returned);

// --- Clicks -----------------------------------------------------------------

// Runs on the node before a click (awaitPromise, returnByValue): waits until
// its box has stopped moving for two frames (a popup that is still opening,
// a list that repositions itself after the scroll into view: why a click on
// the option of a list just opened could land beside it), at most about a
// second, then says what is at its centre: {stable, covered}, `covered`
// describing what is drawn over the node there ("" if the node, something in
// it or one of its labels), as the page could see it.
extern const char kClickReadyFunction[];
struct ClickReadiness {
  bool stable = true;
  std::string covered;
};
ClickReadiness ReadClickReadiness(const base::Value* returned);

// --- choose: any dropdown --------------------------------------------------

// On the ref's node: {kind: "select"} for a <select> (or one of its
// options), {kind: "popup", expanded, editable} for what opens a list (a
// combobox, a button with aria-haspopup, an element that controls a
// listbox), else {kind: "other", what}.
extern const char kChooseKindFunction[];
// On the ref's node, function(pick, label): the list of options it opened,
// found as a screen reader finds it: the elements its (or its descendants')
// aria-controls and aria-owns name, a listbox inside it, else the last shown
// listbox or menu of the document (a portal, appended at the end of the
// body). pick < 0: {found, mine, options: [{label, disabled}], more}
// (`mine`: one of the first two kinds); else the
// option at `pick` (not by value) if its label is still `label`, else null.
extern const char kListOptionsFunction[];
// On the ref's node, function(label): {has, now}: whether the control (its
// value, its text, or its box's, a few levels up) shows `label` now, and
// what it shows.
extern const char kChosenValueFunction[];

enum class ChooseKind { kSelect, kPopup, kRefused };
struct ChooseTarget {
  ChooseKind kind = ChooseKind::kRefused;
  bool expanded = false;
  // A text field (an autocomplete): typing filters its options.
  bool editable = false;
  std::string refusal;
};
ChooseTarget ReadChooseTarget(const base::Value* returned,
                              std::string_view ref);

struct OptionList {
  bool found = false;
  // The list is the control's own (named by it, or inside it), not just
  // the last one the page shows.
  bool mine = false;
  std::vector<std::string> labels;
  std::vector<bool> disabled;
  int more = 0;
};
OptionList ReadOptionList(const base::Value* returned);

struct OptionMatch {
  std::optional<size_t> index;
  // No index: the error, naming the options.
  std::string error;
};
// The option `wanted` names: the first whose label is it (whitespace
// collapsed), else the first that is it in any case, else the only one that
// starts with it, else the only one that contains it (any case). A disabled
// match, several partial ones, or none, is an error that lists the options.
OptionMatch MatchOption(const OptionList& list,
                        std::string_view wanted,
                        std::string_view ref);
// No list of options appeared after `ref` was opened.
std::string NoListText(std::string_view ref);
// The option was clicked, but the control does not show it (`now`).
std::string NotChosenText(std::string_view ref,
                          std::string_view label,
                          std::string_view now);

// An error as an agent is shown it: Koan's own errors as they are, a raw CDP
// error (a number, or CDP's own data) as the Koan error it amounts to, with
// what to do next.
struct AgentError {
  std::string code;
  std::string message;
};
AgentError MapError(std::string_view code, std::string_view message);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_ACTIONS_H_
