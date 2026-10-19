// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_ROWS_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_ROWS_H_

#include <string_view>

#include "base/containers/span.h"
#include "base/memory/raw_ptr_exclusion.h"

// What chrome://koan-settings shows, as data. The page lays out Zen's
// preferences: panes in the sidebar, each with sections (big headings), each
// with groups (a header, a description and the settings). Every setting is a
// row bound to a Koan profile pref; the page applies a change by setting the
// pref, and features pick it up live by observing their prefs.
//
// To add a setting for a feature, add a Row to kRows in
// koan_settings_rows.cc, in its group (add the group if Zen has one that
// isn't here yet). Groups without rows aren't shown. The page can only
// change prefs listed here.
namespace koan::settings {

struct Pane {
  const char* id;
  const char* title;
};

struct Group {
  const char* id;
  // Pane::id.
  const char* pane;
  // The section heading. Consecutive groups with the same section share it.
  const char* section;
  const char* header;
  const char* description;
};

enum class Control {
  // A boolean pref.
  kCheckbox,
  // A labelled drop-down of `options`; boolean, integer or string pref.
  kMenu,
  // Zen's browser layout cards: one card per option, with a preview.
  kLayoutPicker,
};

struct Option {
  // The pref value as text: "true"/"false", a number or the string itself.
  const char* value;
  const char* label;
  // For kLayoutPicker: which preview to draw (see koan_settings.css).
  const char* preview = nullptr;
};

struct Row {
  // Group::id.
  const char* group;
  Control control;
  const char* pref;
  const char* label;
  // RAW_PTR_EXCLUSION: #global-scope (rows live in a constexpr table).
  RAW_PTR_EXCLUSION base::span<const Option> options = {};
  // A boolean pref (in this table) that greys this row out while false,
  // like Zen's dependent checkboxes.
  const char* depends_on = nullptr;
  // Whether the setting applies at all, e.g. its feature is on in
  // chrome://flags. Rows that don't apply are hidden. Null: always.
  bool (*is_available)() = nullptr;
  // Indented under the row above, like Zen's sub-options.
  bool indent = false;
};

// The shortcut editor's pane; the page fills it from the shortcut table.
inline constexpr char kShortcutsPane[] = "shortcuts";

base::span<const Pane> Panes();
base::span<const Group> Groups();
base::span<const Row> Rows();

// Returns null if `pref` has no row.
const Row* FindRow(std::string_view pref);

}  // namespace koan::settings

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_ROWS_H_
