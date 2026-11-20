// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_snapshot.h"

#include <algorithm>
#include <utility>

#include "base/containers/adapters.h"
#include "base/containers/fixed_flat_set.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "koan/cli/koan_cli_batch.h"

namespace koan::cli {

// Whitespace runs as one space, none at the ends.
std::string CollapseSpace(std::string_view text) {
  std::string out;
  bool space = false;
  for (char c : text) {
    if (base::IsAsciiWhitespace(c)) {
      space = !out.empty();
      continue;
    }
    if (space) {
      out += ' ';
      space = false;
    }
    out += c;
  }
  return out;
}

namespace {

// The longest name or value shown, and the longest joined text.
constexpr size_t kMaxName = 120;
constexpr size_t kMaxText = 200;
// What a holder keeps, so that a hostile page cannot grow it without bound.
constexpr size_t kMaxPages = 64;
constexpr size_t kMaxRefsPerPage = 200000;

// Roles (as Accessibility.getFullAXTree names them: ARIA roles where there is
// one, Blink's internal names otherwise) an agent acts on: always shown, with
// a ref.
constexpr auto kActionRoles = base::MakeFixedFlatSet<std::string_view>({
    "button",        "checkbox", "combobox",   "DisclosureTriangle",
    "link",          "listbox",  "menuitem",   "menuitemcheckbox",
    "menuitemradio", "option",   "radio",      "scrollbar",
    "searchbox",     "slider",   "spinbutton", "switch",
    "tab",           "textbox",  "treeitem",
});

// Landmarks and what an agent finds its way by: always shown, with a ref.
constexpr auto kLandmarkRoles = base::MakeFixedFlatSet<std::string_view>({
    "alertdialog",
    "banner",
    "complementary",
    "contentinfo",
    "dialog",
    "form",
    "heading",
    "Iframe",
    "main",
    "navigation",
    "region",
    "RootWebArea",
    "search",
});

// Roles whose name is their text: text inside them that only repeats it is
// left out.
constexpr auto kNamedByContent = base::MakeFixedFlatSet<std::string_view>({
    "button",
    "DisclosureTriangle",
    "heading",
    "link",
    "menuitem",
    "menuitemcheckbox",
    "menuitemradio",
    "option",
    "tab",
    "treeitem",
});

// Blocks: never shown unless an agent can act on them (a table cell's or a
// row's name is only its text, which is shown anyway), but the end of a text
// run.
constexpr auto kBlockRoles = base::MakeFixedFlatSet<std::string_view>({
    "blockquote",   "caption",    "cell",
    "columnheader", "definition", "DescriptionList",
    "Details",      "gridcell",   "group",
    "LabelText",    "list",       "listitem",
    "log",          "note",       "paragraph",
    "Pre",          "row",        "rowgroup",
    "rowheader",    "status",     "term",
});

// Inline elements: their text flows on with the text around them.
constexpr auto kInlineRoles = base::MakeFixedFlatSet<std::string_view>({
    "abbr",
    "code",
    "deletion",
    "emphasis",
    "insertion",
    "mark",
    "strong",
    "subscript",
    "superscript",
    "time",
});

// --interactive: the lines shown besides what an agent acts on, for where
// it is.
constexpr auto kInteractiveContext = base::MakeFixedFlatSet<std::string_view>({
    "alertdialog",
    "dialog",
    "heading",
    "Iframe",
    "RootWebArea",
});

// --interactive: messages whose text is shown (an error after a submit, a
// "Saved").
constexpr auto kMessageRoles = base::MakeFixedFlatSet<std::string_view>({
    "alert",
    "log",
    "status",
});

// An AX value ({"type", "value"}) as text: strings as they are, numbers and
// booleans written out.
std::string ValueText(const base::DictValue* ax_value) {
  if (!ax_value) {
    return std::string();
  }
  const base::Value* value = ax_value->Find("value");
  if (!value) {
    return std::string();
  }
  if (value->is_string()) {
    return value->GetString();
  }
  if (value->is_bool()) {
    return value->GetBool() ? "true" : "false";
  }
  if (value->is_int()) {
    return base::NumberToString(value->GetInt());
  }
  if (value->is_double()) {
    return base::NumberToString(value->GetDouble());
  }
  return std::string();
}

std::string AxString(const base::DictValue& node, std::string_view key) {
  return ValueText(node.FindDict(key));
}

std::string Quoted(std::string_view text, size_t max) {
  std::string out = "\"";
  size_t count = 0;
  for (char c : text) {
    // Counted in bytes, but never cut inside a UTF-8 sequence.
    if (count >= max && (static_cast<unsigned char>(c) & 0xC0) != 0x80) {
      out += "...";
      break;
    }
    ++count;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n' || c == '\r' || c == '\t') {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
  return out;
}

// The value of the AX property `name` (properties: [{name, value}]).
std::string Property(const base::DictValue& node, std::string_view name) {
  const base::ListValue* properties = node.FindList("properties");
  if (!properties) {
    return std::string();
  }
  for (const base::Value& property : *properties) {
    const base::DictValue* dict = property.GetIfDict();
    const std::string* key = dict ? dict->FindString("name") : nullptr;
    if (key && *key == name) {
      return ValueText(dict->FindDict("value"));
    }
  }
  return std::string();
}

bool IsQuietRole(std::string_view role) {
  return role.empty() || role == "generic" || role == "none" ||
         role == "presentation";
}

class Outline {
 public:
  Outline(const base::DictValue& ax_tree, const SnapshotOptions& options)
      : options_(options) {
    const base::ListValue* nodes = ax_tree.FindList("nodes");
    if (!nodes) {
      return;
    }
    for (const base::Value& value : *nodes) {
      const base::DictValue* node = value.GetIfDict();
      if (!node) {
        continue;
      }
      if (const std::string* id = node->FindString("nodeId")) {
        by_id_.emplace(*id, node);
        if (const base::ListValue* children = node->FindList("childIds")) {
          for (const base::Value& child : *children) {
            if (child.is_string()) {
              parent_.emplace(child.GetString(), *id);
            }
          }
        }
      }
      if (std::optional<int> backend = node->FindInt("backendDOMNodeId")) {
        refs_.insert(*backend);
        if (options_.locators && !node->FindBool("ignored").value_or(false)) {
          ++names_[{AxString(*node, "role"),
                    CollapseSpace(AxString(*node, "name"))}];
        }
      }
      if (!root_) {
        root_ = node;
      }
    }
  }

  Snapshot Build() {
    Snapshot snapshot;
    const base::DictValue* start = root_;
    if (options_.root) {
      start = nullptr;
      for (const auto& [id, node] : by_id_) {
        if (node->FindInt("backendDOMNodeId") == options_.root) {
          start = node;
          break;
        }
      }
      snapshot.root_missing = !start;
    }
    if (start) {
      if (!options_.full && !options_.root) {
        pinned_ = true;
        WalkPopups();
        pinned_ = false;
      }
      Walk(start, 0);
    }
    snapshot.text = std::move(out_);
    if (truncated_) {
      snapshot.text += "... (more; the page is long)\n";
    }
    snapshot.truncated = truncated_;
    snapshot.refs.assign(refs_.begin(), refs_.end());
    snapshot.lines = std::move(lines_out_);
    return snapshot;
  }

 private:
  struct Step {
    enum Kind {
      kNode,
      // The end of a block: it ends the text in it.
      kBlockEnd,
      // The end of a wrapper (a <div> or a <span>, which the tree does not
      // tell apart): text after it is a new word.
      kWrapperEnd,
    };
    raw_ptr<const base::DictValue> node = nullptr;
    int depth = 0;
    Kind kind = kNode;
    // The name of the shown node this one is in, if its name is its text:
    // text that only repeats it is left out.
    std::string within;
    // --interactive: inside an alert or a status message, whose text is
    // shown.
    bool keep_text = false;
  };

  // Open popups first (compact only), so that the options a click has just
  // shown are on top and never cut off a long page: a listbox or menu that
  // an expanded control names (aria-controls, aria-owns, a popover's
  // details) or, while a control is expanded, one that sits outside the
  // page's layout (a portal: nothing but wrappers between it and the
  // document), and every dialog. Each is
  // walked here, under a "popup" line naming its opener, and not again
  // where the page has it.
  void WalkPopups() {
    // Backend id -> the expanded control that names it.
    std::map<int, const base::DictValue*> opened;
    bool any_expanded = false;
    for (const auto& [id, node] : by_id_) {
      if (Property(*node, "expanded") != "true") {
        continue;
      }
      any_expanded = true;
      for (int named : Related(*node, {"controls", "owns", "details"})) {
        opened.emplace(named, node);
      }
    }
    std::vector<const base::DictValue*> popups;
    for (const auto& [id, node] : by_id_) {
      if (node->FindBool("ignored").value_or(false) || node == root_) {
        continue;
      }
      const std::string role = AxString(*node, "role");
      const std::optional<int> backend = node->FindInt("backendDOMNodeId");
      const bool named = backend && opened.contains(*backend);
      const bool popup = role == "dialog" || role == "alertdialog" ||
                         ((role == "listbox" || role == "menu") &&
                          (named || (any_expanded && AtTop(id))));
      if (popup && !InsidePopupRole(id)) {
        popups.push_back(node);
      }
    }
    // In the page's order (node ids are not), by the order of a walk.
    std::sort(popups.begin(), popups.end(),
              [&](auto* a, auto* b) { return Order(a) < Order(b); });
    for (const base::DictValue* popup : popups) {
      const std::optional<int> backend = popup->FindInt("backendDOMNodeId");
      auto opener = backend ? opened.find(*backend) : opened.end();
      std::string header = "popup";
      if (opener != opened.end()) {
        const base::DictValue& by = *opener->second;
        header += " of " + AxString(by, "role");
        const std::string name = CollapseSpace(AxString(by, "name"));
        if (!name.empty()) {
          header += " " + Quoted(name, kMaxName);
        }
        if (std::optional<int> id = by.FindInt("backendDOMNodeId")) {
          header += " @" + base::NumberToString(*id);
        }
      }
      header += " (open; shown first, the page has it further down):";
      Flush();
      Emit(0, header);
      Walk(popup, 1);
      Flush();
      last_name_.clear();
    }
  }

  // The backend ids an AX relation property (an idref list) names.
  static std::vector<int> Related(
      const base::DictValue& node,
      std::initializer_list<std::string_view> names) {
    std::vector<int> ids;
    const base::ListValue* properties = node.FindList("properties");
    if (!properties) {
      return ids;
    }
    for (const base::Value& property : *properties) {
      const base::DictValue* dict = property.GetIfDict();
      const std::string* key = dict ? dict->FindString("name") : nullptr;
      if (!key || std::find(names.begin(), names.end(), *key) == names.end()) {
        continue;
      }
      const base::ListValue* related =
          dict->FindListByDottedPath("value.relatedNodes");
      if (!related) {
        continue;
      }
      for (const base::Value& entry : *related) {
        const base::DictValue* r = entry.GetIfDict();
        if (std::optional<int> id =
                r ? r->FindInt("backendDOMNodeId") : std::nullopt) {
          ids.push_back(*id);
        }
      }
    }
    return ids;
  }

  // Only wrappers between the node and the document.
  bool AtTop(const std::string& id) const {
    std::string at = id;
    for (int hops = 0; hops < 64; ++hops) {
      auto up = parent_.find(at);
      if (up == parent_.end()) {
        return false;
      }
      auto node = by_id_.find(up->second);
      if (node == by_id_.end()) {
        return false;
      }
      if (node->second == root_) {
        return true;
      }
      const std::string role = AxString(*node->second, "role");
      if (!node->second->FindBool("ignored").value_or(false) &&
          !IsQuietRole(role)) {
        return false;
      }
      at = up->second;
    }
    return false;
  }

  // Inside another dialog, listbox or menu: shown with it.
  bool InsidePopupRole(const std::string& id) const {
    std::string at = id;
    for (int hops = 0; hops < 256; ++hops) {
      auto up = parent_.find(at);
      if (up == parent_.end()) {
        return false;
      }
      auto node = by_id_.find(up->second);
      if (node == by_id_.end()) {
        return false;
      }
      const std::string role = AxString(*node->second, "role");
      if (role == "dialog" || role == "alertdialog" || role == "listbox" ||
          role == "menu") {
        return true;
      }
      at = up->second;
    }
    return false;
  }

  // A node's place in a depth-first walk of the tree, computed once.
  size_t Order(const base::DictValue* node) {
    if (order_.empty() && root_) {
      std::vector<const base::DictValue*> stack = {root_.get()};
      std::set<const base::DictValue*> seen;
      while (!stack.empty()) {
        const base::DictValue* at = stack.back();
        stack.pop_back();
        if (!seen.insert(at).second) {
          continue;
        }
        order_.emplace(at, order_.size());
        if (const base::ListValue* children = at->FindList("childIds")) {
          for (const base::Value& child : base::Reversed(*children)) {
            const std::string* child_id = child.GetIfString();
            auto it = child_id ? by_id_.find(*child_id) : by_id_.end();
            if (it != by_id_.end()) {
              stack.push_back(it->second);
            }
          }
        }
      }
    }
    auto it = order_.find(node);
    return it == order_.end() ? order_.size() : it->second;
  }

  // Depth-first, without recursion (a hostile page can nest deeply).
  void Walk(const base::DictValue* start, int start_depth) {
    std::vector<Step> stack;
    stack.push_back({start, start_depth, Step::kNode, std::string(), false});
    while (!stack.empty() && !truncated_) {
      Step step = std::move(stack.back());
      stack.pop_back();
      if (step.kind == Step::kBlockEnd) {
        Flush();
        continue;
      }
      if (step.kind == Step::kWrapperEnd) {
        word_break_ = true;
        continue;
      }
      if (!seen_.insert(step.node.get()).second) {
        continue;
      }
      const base::DictValue& node = *step.node;
      int child_depth = step.depth;
      std::string within = step.within;
      bool keep_text = step.keep_text;
      Step::Kind end = Step::kNode;
      const bool descend = options_.full
                               ? Full(node, step.depth, &child_depth)
                               : Compact(node, step.depth, &child_depth,
                                         &within, &keep_text, &end);
      if (!descend) {
        continue;
      }
      if (end != Step::kNode) {
        stack.push_back({nullptr, step.depth, end, std::string(), false});
      }
      const base::ListValue* children = node.FindList("childIds");
      if (!children) {
        continue;
      }
      for (const base::Value& child : base::Reversed(*children)) {
        const std::string* child_id = child.GetIfString();
        auto it = child_id ? by_id_.find(*child_id) : by_id_.end();
        if (it != by_id_.end()) {
          stack.push_back(
              {it->second, child_depth, Step::kNode, within, keep_text});
        }
      }
    }
    Flush();
  }

  // --full: every node but the ignored and unnamed wrappers, a ref on each.
  bool Full(const base::DictValue& node, int depth, int* child_depth) {
    if (node.FindBool("ignored").value_or(false)) {
      return true;
    }
    const std::string role = AxString(node, "role");
    const std::string name = AxString(node, "name");
    const std::string value = AxString(node, "value");
    const std::string states = StatesText(node);
    if (IsQuietRole(role) && name.empty() && value.empty() && states.empty()) {
      return true;
    }
    Line(depth, node, role, name, value, states, /*with_ref=*/true);
    *child_depth = depth + 1;
    return true;
  }

  // The compact outline. Returns whether to go into the node's children;
  // `end` is what ends them.
  bool Compact(const base::DictValue& node,
               int depth,
               int* child_depth,
               std::string* within,
               bool* keep_text,
               Step::Kind* end) {
    if (node.FindBool("ignored").value_or(false)) {
      return true;
    }
    const std::string role = AxString(node, "role");
    if (role == "InlineTextBox" || role == "ListMarker") {
      return false;
    }
    if (role == "StaticText" || role == "LineBreak") {
      if (options_.interactive && !*keep_text) {
        return false;
      }
      // A line break's name is "\n": a space between the runs around it.
      const std::string text = AxString(node, "name");
      const std::string collapsed = CollapseSpace(text);
      if (!collapsed.empty() && !within->empty() &&
          within->find(collapsed) != std::string::npos) {
        return false;
      }
      AddText(depth, text, node.FindInt("backendDOMNodeId").value_or(0));
      return false;
    }
    const std::string name = AxString(node, "name");
    const std::string value = AxString(node, "value");
    const bool action = kActionRoles.contains(role) ||
                        Property(node, "focusable") == "true" ||
                        !Property(node, "editable").empty();
    const bool landmark = kLandmarkRoles.contains(role);
    const bool wrapper = IsQuietRole(role);
    const bool inline_role = kInlineRoles.contains(role);
    // Any other role is shown (without a ref) if it has a name: an image, a
    // table, a tab panel, an alert, a video.
    const bool shown =
        options_.interactive
            ? action || kInteractiveContext.contains(role)
            : action || landmark ||
                  ((!name.empty() || !value.empty()) && !wrapper &&
                   !inline_role && !kBlockRoles.contains(role));
    if (options_.interactive && kMessageRoles.contains(role)) {
      *keep_text = true;
    }
    if (!shown) {
      if (wrapper) {
        word_break_ = true;
        *end = Step::kWrapperEnd;
      } else if (!inline_role) {
        Flush();
        *end = Step::kBlockEnd;
      }
      return true;
    }
    // Text just before a control that is its label ("Plan", and then
    // combobox "Plan") says nothing more.
    const std::string collapsed_name = CollapseSpace(name);
    if (!collapsed_name.empty() && CollapseSpace(pending_) == collapsed_name) {
      pending_.clear();
    }
    Flush();
    Line(depth, node, role, name, value, StatesText(node), action || landmark);
    last_name_ = collapsed_name;
    *child_depth = depth + 1;
    *end = Step::kBlockEnd;
    if (kNamedByContent.contains(role) && !collapsed_name.empty()) {
      *within = collapsed_name;
    }
    // A closed dropdown's options: its value is on its line.
    if (options_.interactive && role == "combobox" &&
        Property(node, "expanded") != "true") {
      Flush();
      return false;
    }
    return true;
  }

  void AddText(int depth, std::string_view text, int node) {
    if (!pending_.empty() && depth != pending_depth_) {
      Flush();
    }
    if (pending_.empty()) {
      pending_depth_ = depth;
      pending_node_ = node;
    } else if (word_break_ && !text.empty() &&
               !base::IsAsciiWhitespace(pending_.back()) &&
               !base::IsAsciiWhitespace(text.front()) &&
               std::string_view(",.;:!?)]}%").find(text.front()) ==
                   std::string_view::npos &&
               std::string_view("([{").find(pending_.back()) ==
                   std::string_view::npos) {
      pending_ += ' ';
    }
    word_break_ = false;
    // Runs are joined as they are: their names keep the spaces the page
    // renders between them.
    pending_ += text;
  }

  void Flush() {
    const std::string text = CollapseSpace(pending_);
    pending_.clear();
    word_break_ = false;
    // A label after its control ("checkbox "Agree"" and then "Agree").
    if (text.empty() || text == last_name_) {
      return;
    }
    if (!Emit(pending_depth_, Quoted(text, kMaxText), pending_node_)) {
      return;
    }
    last_name_.clear();
  }

  void Line(int depth,
            const base::DictValue& node,
            const std::string& role,
            const std::string& name,
            const std::string& value,
            const std::string& states,
            bool with_ref) {
    std::string line = role;
    if (!name.empty()) {
      line +=
          " " + Quoted(options_.full ? name : CollapseSpace(name), kMaxName);
    }
    if (!value.empty()) {
      line += " = " + Quoted(value, kMaxName);
    }
    if (!states.empty()) {
      line += " " + states;
    }
    const int backend = node.FindInt("backendDOMNodeId").value_or(0);
    if (with_ref && backend) {
      line += " @" + base::NumberToString(backend);
      const std::string collapsed = CollapseSpace(name);
      if (options_.locators && &node != root_.get() && !collapsed.empty() &&
          names_[{role, collapsed}] == 1) {
        line += " " + LocatorText(role, collapsed);
      }
    }
    Emit(depth, line, backend, &node == root_.get());
  }

  bool Emit(int depth,
            const std::string& line,
            int node = 0,
            bool pinned = false) {
    if (lines_ == options_.max_lines) {
      truncated_ = true;
      return false;
    }
    out_.append(static_cast<size_t>(depth) * 2, ' ');
    out_ += line;
    out_ += '\n';
    ++lines_;
    lines_out_.push_back({node, depth, line, pinned || pinned_});
    return true;
  }

  const SnapshotOptions options_;
  std::map<std::string, const base::DictValue*> by_id_;
  // Node id -> its parent's.
  std::map<std::string, std::string> parent_;
  std::map<const base::DictValue*, size_t> order_;
  // Walked already (a popup, before the page).
  std::set<const base::DictValue*> seen_;
  raw_ptr<const base::DictValue> root_ = nullptr;
  std::set<int> refs_;
  // --locators: how many nodes have each role and name.
  std::map<std::pair<std::string, std::string>, int> names_;
  std::string out_;
  std::vector<SnapshotLine> lines_out_;
  size_t lines_ = 0;
  // Walking the open popups, which every scope shows.
  bool pinned_ = false;
  bool truncated_ = false;
  // Text runs not written yet, and their depth.
  std::string pending_;
  int pending_depth_ = 0;
  // The first run's node.
  int pending_node_ = 0;
  // A wrapper began or ended since the last run.
  bool word_break_ = false;
  // The name of the last line written, if it was a node's.
  std::string last_name_;
};

}  // namespace

Snapshot BuildSnapshot(const base::DictValue& ax_tree,
                       const SnapshotOptions& options) {
  return Outline(ax_tree, options).Build();
}

std::string StatesText(const base::DictValue& ax_node) {
  std::vector<std::string> states;
  const std::string checked = Property(ax_node, "checked");
  if (checked == "true") {
    states.push_back("checked");
  } else if (checked == "mixed") {
    states.push_back("mixed");
  }
  const std::string pressed = Property(ax_node, "pressed");
  if (pressed == "true") {
    states.push_back("pressed");
  } else if (pressed == "mixed") {
    states.push_back("mixed");
  }
  if (Property(ax_node, "selected") == "true") {
    states.push_back("selected");
  }
  const std::string expanded = Property(ax_node, "expanded");
  if (expanded == "true") {
    states.push_back("expanded");
  } else if (expanded == "false") {
    states.push_back("collapsed");
  }
  for (const char* flag : {"disabled", "required", "readonly"}) {
    if (Property(ax_node, flag) == "true") {
      states.push_back(flag);
    }
  }
  const std::string invalid = Property(ax_node, "invalid");
  if (!invalid.empty() && invalid != "false") {
    states.push_back("invalid");
  }
  for (const char* flag : {"focused", "modal"}) {
    if (Property(ax_node, flag) == "true") {
      states.push_back(flag);
    }
  }
  if (AxString(ax_node, "role") == "heading") {
    const std::string level = Property(ax_node, "level");
    if (!level.empty()) {
      states.push_back("level " + level);
    }
  }
  if (states.empty()) {
    return std::string();
  }
  return "[" + base::JoinString(states, ", ") + "]";
}

std::string RenderWindow(const Snapshot& snapshot, size_t first, size_t end) {
  std::string out;
  size_t above = 0;
  size_t below = 0;
  auto emit = [&](const SnapshotLine& line) {
    out.append(static_cast<size_t>(line.depth) * 2, ' ');
    out += line.text;
    out += '\n';
  };
  // The page's own line, and the popups, come first: above's count goes
  // after them.
  size_t at = 0;
  for (; at < snapshot.lines.size() && snapshot.lines[at].pinned; ++at) {
    emit(snapshot.lines[at]);
  }
  for (size_t i = at; i < snapshot.lines.size() && i < first; ++i) {
    if (!snapshot.lines[i].pinned) {
      ++above;
    }
  }
  if (above) {
    out += "[more above: " + base::NumberToString(above) + " nodes]\n";
  }
  for (size_t i = at; i < snapshot.lines.size(); ++i) {
    const SnapshotLine& line = snapshot.lines[i];
    if (line.pinned || (i >= first && i < end)) {
      emit(line);
    } else if (i >= end) {
      ++below;
    }
  }
  if (below || snapshot.truncated) {
    out += "[more below: " + base::NumberToString(below) +
           (snapshot.truncated ? "+" : "") + " nodes]\n";
  }
  return out;
}

// --- Diffs
// ---------------------------------------------------------------------

namespace {

// What a line is the same line by: its node, else (a popup's heading) its
// text.
std::string LineKey(const SnapshotLine& line) {
  return line.node ? "n" + base::NumberToString(line.node) : "t" + line.text;
}

// What a baseline keeps of a snapshot, so that a hostile page cannot grow
// the holder without bound.
constexpr size_t kMaxBaselinePages = 16;
constexpr size_t kMaxBaselineLines = 20000;

}  // namespace

std::string BaselineKind(const SnapshotOptions& options) {
  std::string kind = options.full          ? "full"
                     : options.interactive ? "interactive"
                                           : "compact";
  if (options.root) {
    kind += " of @" + base::NumberToString(options.root);
  }
  return kind;
}

base::DictValue BaselineToValue(const Baseline& baseline) {
  base::ListValue lines;
  for (const SnapshotLine& line : baseline.lines) {
    lines.Append(base::ListValue().Append(line.node).Append(line.text));
  }
  return base::DictValue()
      .Set("document", baseline.document)
      .Set("kind", baseline.kind)
      .Set("lines", std::move(lines));
}

std::optional<Baseline> BaselineFromValue(const base::DictValue& value) {
  const std::string* document = value.FindString("document");
  const std::string* kind = value.FindString("kind");
  const base::ListValue* lines = value.FindList("lines");
  if (!document || !kind || !lines) {
    return std::nullopt;
  }
  Baseline baseline;
  baseline.document = *document;
  baseline.kind = *kind;
  for (const base::Value& entry : *lines) {
    const base::ListValue* pair = entry.GetIfList();
    if (!pair || pair->size() != 2 || !(*pair)[0].is_int() ||
        !(*pair)[1].is_string()) {
      return std::nullopt;
    }
    if (baseline.lines.size() == kMaxBaselineLines) {
      break;
    }
    SnapshotLine line;
    line.node = (*pair)[0].GetInt();
    line.text = (*pair)[1].GetString();
    baseline.lines.push_back(std::move(line));
  }
  return baseline;
}

Baselines::Baselines() = default;
Baselines::~Baselines() = default;

std::optional<Baseline> Baselines::Swap(const std::string& target,
                                        Baseline next) {
  if (next.lines.size() > kMaxBaselineLines) {
    next.lines.resize(kMaxBaselineLines);
  }
  auto it = pages_.find(target);
  if (it == pages_.end()) {
    if (pages_.size() >= kMaxBaselinePages) {
      pages_.erase(pages_.begin());
    }
    pages_.emplace(target, std::move(next));
    return std::nullopt;
  }
  std::optional<Baseline> previous = std::move(it->second);
  it->second = std::move(next);
  return previous;
}

std::string DiffText(const std::optional<Baseline>& previous,
                     const Baseline& now,
                     std::string_view full_text) {
  std::string reset;
  if (!previous) {
    reset = "no earlier snapshot of this tab";
  } else if (previous->document != now.document) {
    reset = "the tab shows a new page";
  } else if (previous->kind != now.kind) {
    reset =
        "the last snapshot was " + previous->kind + ", this one is " + now.kind;
  }
  if (!reset.empty()) {
    return "baseline reset (" + reset + "); the whole snapshot:\n" +
           std::string(full_text);
  }
  std::map<std::string, const SnapshotLine*> before;
  for (const SnapshotLine& line : previous->lines) {
    before.emplace(LineKey(line), &line);
  }
  std::set<std::string> kept;
  std::string body;
  size_t added = 0;
  size_t changed = 0;
  size_t removed = 0;
  for (const SnapshotLine& line : now.lines) {
    const std::string key = LineKey(line);
    auto it = before.find(key);
    if (it == before.end()) {
      body += "+ " + line.text + "\n";
      ++added;
      continue;
    }
    kept.insert(key);
    if (it->second->text != line.text) {
      body += "~ " + line.text + "\n";
      ++changed;
    }
  }
  for (const SnapshotLine& line : previous->lines) {
    if (!kept.contains(LineKey(line))) {
      body += "- " + line.text + "\n";
      ++removed;
    }
  }
  if (!added && !changed && !removed) {
    return "diff: no change since the last snapshot of this page\n";
  }
  return "diff since the last snapshot of this page: " +
         base::NumberToString(added) + " added, " +
         base::NumberToString(changed) + " changed, " +
         base::NumberToString(removed) + " removed\n" + body;
}

// --- Refs --------------------------------------------------------------------

std::string_view RefVerdictName(RefVerdict verdict) {
  switch (verdict) {
    case RefVerdict::kOk:
      return "ok";
    case RefVerdict::kNoSnapshot:
      return "noSnapshot";
    case RefVerdict::kOtherDocument:
      return "otherDocument";
    case RefVerdict::kUnknown:
      return "unknown";
    case RefVerdict::kResumed:
      return "resumed";
  }
  return "unknown";
}

std::optional<RefVerdict> RefVerdictFromName(std::string_view name) {
  for (RefVerdict verdict :
       {RefVerdict::kOk, RefVerdict::kNoSnapshot, RefVerdict::kOtherDocument,
        RefVerdict::kUnknown, RefVerdict::kResumed}) {
    if (RefVerdictName(verdict) == name) {
      return verdict;
    }
  }
  return std::nullopt;
}

RefBook::RefBook() = default;
RefBook::~RefBook() = default;

void RefBook::Remember(const std::string& target,
                       const std::string& document,
                       const std::vector<int>& refs) {
  auto it = pages_.find(target);
  if (it == pages_.end()) {
    if (pages_.size() >= kMaxPages) {
      pages_.erase(pages_.begin());
    }
    it = pages_.emplace(target, Page()).first;
  }
  Page& page = it->second;
  if (page.document != document || page.resumed) {
    page.document = document;
    page.refs.clear();
    page.resumed = false;
  }
  for (int ref : refs) {
    if (page.refs.size() >= kMaxRefsPerPage) {
      break;
    }
    page.refs.insert(ref);
  }
}

RefVerdict RefBook::Check(const std::string& target,
                          const std::string& document,
                          int ref) const {
  auto it = pages_.find(target);
  if (it == pages_.end()) {
    return RefVerdict::kNoSnapshot;
  }
  if (it->second.resumed) {
    return RefVerdict::kResumed;
  }
  if (document.empty() || it->second.document != document) {
    return RefVerdict::kOtherDocument;
  }
  return it->second.refs.contains(ref) ? RefVerdict::kOk : RefVerdict::kUnknown;
}

void RefBook::Forget(const std::string& target) {
  pages_.erase(target);
}

void RefBook::Resumed(const std::string& target) {
  auto it = pages_.find(target);
  if (it != pages_.end()) {
    it->second.refs.clear();
    it->second.resumed = true;
  }
}

std::string RefRefusal(RefVerdict verdict,
                       std::string_view ref,
                       std::string_view tab) {
  const std::string again = "take a new snapshot (koan snapshot " +
                            std::string(tab) + ") and use its refs";
  switch (verdict) {
    case RefVerdict::kNoSnapshot:
      return "Koan.staleRef: " + std::string(ref) +
             " is not from a snapshot of this tab (none since the connection "
             "started); " +
             again;
    case RefVerdict::kOtherDocument:
      return "Koan.staleRef: " + std::string(ref) +
             " is from the page this tab showed before (it has navigated or "
             "reloaded since); " +
             again;
    case RefVerdict::kUnknown:
      return "Koan.staleRef: " + std::string(ref) +
             " is not in any snapshot of this page; " + again;
    case RefVerdict::kResumed:
      return "Koan.staleRef: " + std::string(ref) +
             " is from before the user used this tab (they may have "
             "changed the page); " +
             again;
    case RefVerdict::kOk:
      break;
  }
  return std::string();
}

}  // namespace koan::cli
