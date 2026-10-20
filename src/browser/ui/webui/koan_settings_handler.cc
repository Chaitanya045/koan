// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_settings_handler.h"

#include <optional>
#include <string_view>
#include <vector>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/ui/accelerator_table.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_shortcut_table.h"
#include "koan/browser/ui/webui/koan_settings_rows.h"
#include "ui/events/event_constants.h"
#include "ui/views/focus/focus_manager.h"

namespace koan {

namespace {

using settings::Control;
using settings::Row;

const char* ControlName(Control control) {
  switch (control) {
    case Control::kCheckbox:
      return "checkbox";
    case Control::kMenu:
      return "menu";
    case Control::kLayoutPicker:
      return "layout";
  }
  NOTREACHED();
}

bool IsAvailable(const Row& row) {
  return !row.is_available || row.is_available();
}

// An Option::value as a value of the pref's type, or nullopt if it isn't one.
std::optional<base::Value> OptionValue(const base::Value& pref_value,
                                       std::string_view text) {
  switch (pref_value.type()) {
    case base::Value::Type::BOOLEAN:
      if (text == "true" || text == "false") {
        return base::Value(text == "true");
      }
      return std::nullopt;
    case base::Value::Type::INTEGER: {
      int number;
      if (base::StringToInt(text, &number)) {
        return base::Value(number);
      }
      return std::nullopt;
    }
    case base::Value::Type::STRING:
      return base::Value(text);
    default:
      return std::nullopt;
  }
}

base::ListValue KeysOf(const std::vector<shortcuts::Binding>& bindings,
                       shortcuts::Action action) {
  base::ListValue keys;
  for (const shortcuts::Binding& binding : bindings) {
    if (binding.action == action) {
      keys.Append(shortcuts::ToPrefValue(binding.accelerator));
    }
  }
  return keys;
}

}  // namespace

KoanSettingsHandler::KoanSettingsHandler(PrefService& prefs) : prefs_(prefs) {}

KoanSettingsHandler::~KoanSettingsHandler() {
  SuspendShortcuts(false);
}

void KoanSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getSettings",
      base::BindRepeating(&KoanSettingsHandler::HandleGetSettings,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setPref", base::BindRepeating(&KoanSettingsHandler::HandleSetPref,
                                     base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setShortcut",
      base::BindRepeating(&KoanSettingsHandler::HandleSetShortcut,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "resetShortcuts",
      base::BindRepeating(&KoanSettingsHandler::HandleResetShortcuts,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setEditing", base::BindRepeating(&KoanSettingsHandler::HandleSetEditing,
                                        base::Unretained(this)));
}

void KoanSettingsHandler::OnJavascriptAllowed() {
  pref_change_registrar_.Init(&*prefs_);
  const auto observe = [this](const char* pref) {
    if (pref && prefs_->FindPreference(pref) &&
        !pref_change_registrar_.IsObserved(pref)) {
      pref_change_registrar_.Add(
          pref, base::BindRepeating(&KoanSettingsHandler::OnPrefChanged,
                                    base::Unretained(this)));
    }
  };
  for (const Row& row : settings::Rows()) {
    observe(row.pref);
    observe(row.depends_on);
    observe(row.note_pref);
  }
  observe(prefs::kShortcuts);
}

void KoanSettingsHandler::OnJavascriptDisallowed() {
  pref_change_registrar_.Reset();
  SuspendShortcuts(false);
}

void KoanSettingsHandler::OnWidgetDestroying(views::Widget* widget) {
  suspended_widget_.Reset();
}

void KoanSettingsHandler::OnPrefChanged(const std::string& pref) {
  if (pref == prefs::kShortcuts) {
    FireWebUIListener("shortcuts-changed", Shortcuts());
    return;
  }
  FireWebUIListener("pref-changed", pref, prefs_->GetValue(pref));
}

void KoanSettingsHandler::HandleGetSettings(const base::ListValue& args) {
  CHECK_EQ(args.size(), 1u);
  AllowJavascript();
  ResolveJavascriptCallback(args[0], Settings());
}

void KoanSettingsHandler::HandleSetPref(const base::ListValue& args) {
  CHECK_EQ(args.size(), 3u);
  AllowJavascript();
  const std::string* name = args[1].GetIfString();
  const Row* row = name ? settings::FindRow(*name) : nullptr;
  const PrefService::Preference* pref =
      row ? prefs_->FindPreference(row->pref) : nullptr;
  bool ok = pref && IsAvailable(*row) && pref->IsUserModifiable() &&
            args[2].type() == pref->GetType();
  if (ok && !row->options.empty()) {
    ok = false;
    for (const settings::Option& option : row->options) {
      if (OptionValue(args[2], option.value) == args[2]) {
        ok = true;
      }
    }
  }
  if (ok) {
    prefs_->Set(row->pref, args[2]);
  }
  ResolveJavascriptCallback(args[0], ok);
}

void KoanSettingsHandler::HandleSetShortcut(const base::ListValue& args) {
  CHECK_EQ(args.size(), 3u);
  AllowJavascript();
  const std::string* id = args[1].GetIfString();
  const shortcuts::Info* info = id ? shortcuts::FindAction(*id) : nullptr;
  ui::Accelerator accelerator;
  bool ok = info != nullptr;
  if (ok && args[2].is_dict()) {
    accelerator = shortcuts::FromPrefValue(args[2].GetDict());
    ok = accelerator.key_code() != ui::VKEY_UNKNOWN;
  } else if (ok && !args[2].is_none()) {
    ok = false;
  }
  // Like Zen, a key can't belong to two shortcuts.
  const std::vector<shortcuts::Binding> bindings =
      shortcuts::Bindings(prefs_->GetDict(prefs::kShortcuts));
  for (const shortcuts::Binding& binding : bindings) {
    if (ok && binding.accelerator == accelerator &&
        binding.action != info->action) {
      ok = false;
    }
  }
  if (ok) {
    ScopedDictPrefUpdate update(&*prefs_, prefs::kShortcuts);
    if (KeysOf(shortcuts::DefaultBindings(), info->action)
            .contains(shortcuts::ToPrefValue(accelerator))) {
      // One of its default keys: back to the defaults (all of them, e.g.
      // both of New Empty Split View's).
      update->Remove(info->id);
    } else {
      update->Set(info->id, shortcuts::ToPrefValue(accelerator));
    }
  }
  ResolveJavascriptCallback(args[0], ok);
}

void KoanSettingsHandler::HandleResetShortcuts(const base::ListValue& args) {
  CHECK_EQ(args.size(), 1u);
  AllowJavascript();
  prefs_->ClearPref(prefs::kShortcuts);
  ResolveJavascriptCallback(args[0], true);
}

void KoanSettingsHandler::HandleSetEditing(const base::ListValue& args) {
  SuspendShortcuts(!args.empty() && args[0].GetIfBool().value_or(false));
}

// The FocusManager has one suspended flag, also used by extension
// keybindings (chrome://extensions/shortcuts suspends every window while it
// records a key). Recording in both pages at once can clear it under the
// other; both recordings are short and need focus, so that is left as is.
void KoanSettingsHandler::SuspendShortcuts(bool suspend) {
  if (views::Widget* widget = suspended_widget_.GetSource()) {
    widget->GetFocusManager()->set_shortcut_handling_suspended(false);
    suspended_widget_.Reset();
  }
  if (!suspend) {
    return;
  }
  views::Widget* widget = views::Widget::GetTopLevelWidgetForNativeView(
      web_ui()->GetWebContents()->GetNativeView());
  if (widget && widget->GetFocusManager()) {
    widget->GetFocusManager()->set_shortcut_handling_suspended(true);
    suspended_widget_.Observe(widget);
  }
}

base::DictValue KoanSettingsHandler::Settings() const {
  base::ListValue panes;
  for (const settings::Pane& pane : settings::Panes()) {
    panes.Append(base::DictValue().Set("id", pane.id).Set("title", pane.title));
  }
  base::ListValue groups;
  for (const settings::Group& group : settings::Groups()) {
    groups.Append(base::DictValue()
                      .Set("id", group.id)
                      .Set("pane", group.pane)
                      .Set("section", group.section)
                      .Set("header", group.header)
                      .Set("description", group.description));
  }
  base::ListValue rows;
  base::DictValue values;
  for (const Row& row : settings::Rows()) {
    const PrefService::Preference* pref = prefs_->FindPreference(row.pref);
    if (!pref || !IsAvailable(row)) {
      continue;
    }
    base::ListValue options;
    for (const settings::Option& option : row.options) {
      std::optional<base::Value> value =
          OptionValue(*pref->GetValue(), option.value);
      if (!value) {
        continue;
      }
      base::DictValue entry = base::DictValue()
                                  .Set("value", std::move(*value))
                                  .Set("label", option.label);
      if (option.preview) {
        entry.Set("preview", option.preview);
      }
      options.Append(std::move(entry));
    }
    base::DictValue entry = base::DictValue()
                                .Set("group", row.group)
                                .Set("control", ControlName(row.control))
                                .Set("pref", row.pref)
                                .Set("label", row.label)
                                .Set("options", std::move(options))
                                .Set("indent", row.indent);
    values.Set(row.pref, pref->GetValue()->Clone());
    if (row.depends_on && prefs_->FindPreference(row.depends_on)) {
      entry.Set("dependsOn", row.depends_on);
      values.Set(row.depends_on, prefs_->GetValue(row.depends_on).Clone());
    }
    if (row.note && prefs_->FindPreference(row.note_pref)) {
      entry.Set("note", row.note);
      entry.Set("notePref", row.note_pref);
      values.Set(row.note_pref, prefs_->GetValue(row.note_pref).Clone());
    }
    rows.Append(std::move(entry));
  }
  return base::DictValue()
      .Set("panes", std::move(panes))
      .Set("groups", std::move(groups))
      .Set("rows", std::move(rows))
      .Set("prefs", std::move(values))
      .Set("shortcutsPane", settings::kShortcutsPane)
      .Set("shortcuts", Shortcuts());
}

base::DictValue KoanSettingsHandler::Shortcuts() const {
  const base::DictValue& overrides = prefs_->GetDict(prefs::kShortcuts);
  const std::vector<shortcuts::Binding> bindings =
      shortcuts::Bindings(overrides);
  const std::vector<shortcuts::Binding> defaults = shortcuts::DefaultBindings();
  base::ListValue groups;
  for (const shortcuts::Group& group : shortcuts::Groups()) {
    groups.Append(
        base::DictValue().Set("id", group.id).Set("title", group.title));
  }
  base::ListValue actions;
  for (const shortcuts::Info& info : shortcuts::Actions()) {
    actions.Append(
        base::DictValue()
            .Set("id", info.id)
            .Set("group", info.group)
            .Set("label", info.label)
            // Hidden, but still counted for conflicts.
            .Set("available", !info.is_available || info.is_available())
            .Set("keys", KeysOf(bindings, info.action))
            .Set("defaults", KeysOf(defaults, info.action))
            .Set("custom", overrides.contains(info.id)));
  }
  // Chromium's own shortcuts, so the editor can say when a key replaces one.
  base::ListValue chromium;
  for (const AcceleratorMapping& mapping : GetAcceleratorList()) {
    chromium.Append(shortcuts::ToPrefValue(
        ui::Accelerator(mapping.keycode, mapping.modifiers)));
  }
  // Keys no shortcut can take.
  base::ListValue tab_cycling;
  for (const shortcuts::CycleKey& key : shortcuts::CycleKeys()) {
    tab_cycling.Append(
        shortcuts::ToPrefValue(ui::Accelerator(key.key, key.modifiers)));
  }
  return base::DictValue()
      .Set("groups", std::move(groups))
      .Set("actions", std::move(actions))
      .Set("chromium", std::move(chromium))
      .Set("tabCycling", std::move(tab_cycling))
      .Set("modifiers", base::DictValue()
                            .Set("shift", ui::EF_SHIFT_DOWN)
                            .Set("ctrl", ui::EF_CONTROL_DOWN)
                            .Set("alt", ui::EF_ALT_DOWN)
                            .Set("meta", ui::EF_COMMAND_DOWN));
}

}  // namespace koan
