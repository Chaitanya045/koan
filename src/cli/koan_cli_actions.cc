// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_actions.h"

#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace koan::cli {

const char kFillTargetFunction[] = R"(function() {
  const tag = this.localName || '';
  const what = tag ? '<' + tag + '>' : 'not an element';
  if (tag === 'select') return {kind: 'select'};
  if (tag === 'input' || tag === 'textarea') {
    const type = tag === 'input' ? String(this.type || 'text') : '';
    if (['button', 'checkbox', 'color', 'file', 'hidden', 'image', 'radio',
         'range', 'reset', 'submit'].includes(type)) {
      return {kind: 'notText', what: '<input type=' + type + '>'};
    }
    if (this.disabled) return {kind: 'disabled', what};
    if (this.readOnly) return {kind: 'readonly', what};
    return {kind: 'text', before: String(this.value),
            secret: type === 'password'};
  }
  if (this.isContentEditable) {
    return {kind: 'text', before: String(this.innerText)};
  }
  return {kind: 'notText', what};
})";

const char kSelectContentsFunction[] = R"(function() {
  if (this.select) { this.select(); return; }
  const range = document.createRange();
  range.selectNodeContents(this);
  const selection = getSelection();
  selection.removeAllRanges();
  selection.addRange(range);
})";

const char kReadTextFunction[] = R"(function() {
  const tag = this.localName;
  return String(tag === 'input' || tag === 'textarea' ? this.value
                                                      : this.innerText);
})";

const char kSelectOptionFunction[] = R"(function(wanted) {
  const norm = s => String(s ?? '').replace(/\s+/g, ' ').trim();
  const label = o => norm(o.label || o.text);
  let select = this;
  let option = null;
  if (this.localName === 'option') {
    option = this;
    select = this.closest('select');
  }
  if (!select || select.localName !== 'select') {
    return {error: 'notSelect',
            what: this.localName ? '<' + this.localName + '>'
                                 : 'not an element'};
  }
  const options = Array.from(select.options);
  if (!option) {
    const w = norm(wanted);
    option = options.find(o => label(o) === w) ||
             options.find(o => o.value === String(wanted)) ||
             options.find(o => label(o).toLowerCase() === w.toLowerCase());
  }
  if (!option) {
    return {error: 'noOption', options: options.slice(0, 20).map(label),
            more: Math.max(0, options.length - 20)};
  }
  const group = option.parentElement;
  if (select.disabled || option.disabled ||
      (group && group.localName === 'optgroup' && group.disabled)) {
    return {error: 'disabled', label: label(option)};
  }
  const already = option.selected;
  if (!already) {
    select.focus({preventScroll: true});
    option.selected = true;
    select.dispatchEvent(new Event('input', {bubbles: true, composed: true}));
    select.dispatchEvent(new Event('change', {bubbles: true}));
  }
  return {ok: true, already, held: option.selected, label: label(option),
          value: String(option.value)};
})";

namespace {

std::string Str(const base::DictValue& dict, std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

std::string Shown(std::string_view text) {
  constexpr size_t kMax = 80;
  std::string out = "\"";
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    // Never cut inside a UTF-8 sequence.
    if (i >= kMax && (static_cast<unsigned char>(c) & 0xC0) != 0x80) {
      return out + "...\"";
    }
    out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
  }
  return out + "\"";
}

}  // namespace

const base::Value* ReturnedValue(const base::DictValue& call_result) {
  if (call_result.Find("exceptionDetails")) {
    return nullptr;
  }
  const base::DictValue* result = call_result.FindDict("result");
  return result ? result->Find("value") : nullptr;
}

FillTarget ReadFillTarget(const base::Value* returned, std::string_view ref) {
  FillTarget target;
  const base::DictValue* dict = returned ? returned->GetIfDict() : nullptr;
  if (!dict) {
    target.refusal = "Koan.pageError: the page did not let " +
                     std::string(ref) +
                     " be read; take a new snapshot and retry";
    return target;
  }
  const std::string kind = Str(*dict, "kind");
  const std::string what = Str(*dict, "what");
  if (kind == "text") {
    target.kind = FillKind::kText;
    target.before = Str(*dict, "before");
    target.secret = dict->FindBool("secret").value_or(false);
  } else if (kind == "select") {
    target.kind = FillKind::kSelect;
  } else if (kind == "disabled") {
    target.refusal = "Koan.disabled: " + std::string(ref) + " (" + what +
                     ") is disabled: it takes no text until the page enables "
                     "it";
  } else if (kind == "readonly") {
    target.refusal =
        "Koan.readOnly: " + std::string(ref) + " (" + what + ") is read-only";
  } else {
    target.refusal =
        "Koan.notEditable: " + std::string(ref) + " is " + what +
        ", which takes no text; fill a textbox, searchbox or editable "
        "element, click a checkbox, radio or button, or select an option of "
        "a dropdown (act <tab> select @ref <option>)";
  }
  return target;
}

FillOutcome CheckFill(std::string_view before,
                      std::string_view now,
                      std::string_view wanted) {
  if (wanted.empty() ? now.empty() : now.find(wanted) != std::string::npos) {
    return FillOutcome::kFilled;
  }
  return now != before ? FillOutcome::kAltered : FillOutcome::kUnchanged;
}

std::string FillOutcomeText(FillOutcome outcome,
                            std::string_view ref,
                            std::string_view now,
                            bool secret) {
  const std::string held = secret ? std::string("other text") : Shown(now);
  switch (outcome) {
    case FillOutcome::kFilled:
      return "ok";
    case FillOutcome::kAltered:
      return "ok: " + std::string(ref) + " now holds " + held +
             " (the page changed what was typed)";
    case FillOutcome::kUnchanged:
      break;
  }
  return "Koan.notFilled: " + std::string(ref) + " still holds " + held +
         ": it did not take the text (a format or length limit, or the page "
         "put it back); take a new snapshot to see the field";
}

SelectOutcome ReadSelectOutcome(const base::Value* returned,
                                std::string_view ref) {
  SelectOutcome outcome;
  const base::DictValue* dict = returned ? returned->GetIfDict() : nullptr;
  if (!dict) {
    outcome.text = "Koan.pageError: the page did not let " + std::string(ref) +
                   " be changed; take a new snapshot and retry";
    return outcome;
  }
  const std::string error = Str(*dict, "error");
  if (error == "notSelect") {
    outcome.text =
        "Koan.notSelect: " + std::string(ref) + " is " + Str(*dict, "what") +
        ", not a dropdown (<select>) or one of its options; fill a text "
        "field, or click a custom dropdown and then its option";
    return outcome;
  }
  if (error == "noOption") {
    std::vector<std::string> labels;
    if (const base::ListValue* options = dict->FindList("options")) {
      for (const base::Value& option : *options) {
        if (option.is_string()) {
          labels.push_back(Shown(option.GetString()));
        }
      }
    }
    std::string list = labels.empty() ? "none" : base::JoinString(labels, ", ");
    if (const int more = dict->FindInt("more").value_or(0); more > 0) {
      list += " and " + base::NumberToString(more) + " more";
    }
    outcome.text =
        "Koan.noOption: " + std::string(ref) +
        " has no option with that label or value; its options: " + list;
    return outcome;
  }
  if (error == "disabled") {
    outcome.text = "Koan.disabled: " + Shown(Str(*dict, "label")) + " in " +
                   std::string(ref) + " is disabled";
    return outcome;
  }
  if (!dict->FindBool("ok").value_or(false)) {
    outcome.text = "Koan.pageError: " + std::string(ref) +
                   " did not answer; take a new snapshot and retry";
    return outcome;
  }
  outcome.label = Str(*dict, "label");
  outcome.value = Str(*dict, "value");
  if (!dict->FindBool("held").value_or(false)) {
    outcome.text = "Koan.notSelected: the page put " + std::string(ref) +
                   " back after " + Shown(outcome.label) +
                   " was chosen; take a new snapshot to see why";
    return outcome;
  }
  outcome.ok = true;
  outcome.text = dict->FindBool("already").value_or(false)
                     ? "ok: " + Shown(outcome.label) + " was already selected"
                     : "ok: selected " + Shown(outcome.label);
  return outcome;
}

bool IsNotSelect(const base::Value* returned) {
  const base::DictValue* dict = returned ? returned->GetIfDict() : nullptr;
  return dict && Str(*dict, "error") == "notSelect";
}

AgentError MapError(std::string_view code, std::string_view message) {
  // Koan's own (the gateway's, the facade's, the holder's) say what to do.
  if (code.starts_with("Koan.") || code.starts_with("Hold.")) {
    return {std::string(code), std::string(message)};
  }
  struct Known {
    std::string_view text;
    std::string_view code;
    std::string_view hint;
  };
  static constexpr Known kKnown[] = {
      {"No node with given id found", "Koan.staleRef",
       "that element is gone from the page; take a new snapshot and use its "
       "refs"},
      {"No node found for given backend id", "Koan.staleRef",
       "that element is gone from the page; take a new snapshot and use its "
       "refs"},
      {"Could not find node with given id", "Koan.staleRef",
       "that element is gone from the page; take a new snapshot and use its "
       "refs"},
      {"Node is detached from document", "Koan.staleRef",
       "that element is no longer on the page; take a new snapshot and use "
       "its refs"},
      {"Could not find object with given id", "Koan.staleRef",
       "that element is gone from the page; take a new snapshot and use its "
       "refs"},
      {"Node does not have a layout object", "Koan.notVisible",
       "that element is not shown (hidden, collapsed, or an option of a "
       "closed dropdown): for a dropdown use act <tab> select @ref <option>; "
       "else open or scroll to what hides it and take a new snapshot"},
      {"Could not compute content quads", "Koan.notVisible",
       "that element takes no space on the page (hidden or collapsed); open "
       "or scroll to what hides it and take a new snapshot"},
      {"Could not compute box model", "Koan.notVisible",
       "that element takes no space on the page (hidden or collapsed); open "
       "or scroll to what hides it and take a new snapshot"},
      {"Element is not focusable", "Koan.notFocusable",
       "that element takes no focus or text; fill a textbox or searchbox "
       "ref, or click this one"},
      {"Node is not an Element", "Koan.notElement",
       "that ref is text, not an element; act on the control or link around "
       "it"},
      {"Cannot find context with specified id", "Koan.pageChanged",
       "the page changed while acting (it navigated or reloaded); take a new "
       "snapshot and retry"},
      {"Execution context was destroyed", "Koan.pageChanged",
       "the page changed while acting (it navigated or reloaded); take a new "
       "snapshot and retry"},
      {"Inspected target navigated or closed", "Koan.pageChanged",
       "the page changed while acting (it navigated or closed); take a new "
       "snapshot and retry"},
      {"Cannot navigate to invalid URL", "Koan.badUrl",
       "that is not a URL Koan can open; give a full one, such as "
       "https://example.com/"},
      {"Session with given id not found", "Koan.tabGone",
       "that tab is gone; koan tabs lists yours"},
      {"No target with given id found", "Koan.tabGone",
       "that tab is gone; koan tabs lists yours"},
      {"Not attached to an active page", "Koan.tabGone",
       "that tab is gone or not ready; koan tabs lists yours"},
      {"wasn't found", "Koan.notSupported",
       "this Koan does not offer that call; update koan and Koan together"},
  };
  for (const Known& known : kKnown) {
    if (message.find(known.text) != std::string_view::npos) {
      return {std::string(known.code),
              std::string(known.hint) + " (" + std::string(message) + ")"};
    }
  }
  return {"Koan.pageError",
          "the page or the browser refused this (" + std::string(message) +
              (code.empty() || code.front() == '-' ? std::string()
                                                   : ": " + std::string(code)) +
              "); take a new snapshot and retry, or try another way"};
}

}  // namespace koan::cli
