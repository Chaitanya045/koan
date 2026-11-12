// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_ACTIONS_H_
#define KOAN_CLI_KOAN_CLI_ACTIONS_H_

#include <string>
#include <string_view>

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
