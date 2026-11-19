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

namespace koan::cli {

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

// Whitespace runs as one space, none at the ends.
std::string Collapse(std::string_view text) {
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
      }
      if (!root_) {
        root_ = node;
      }
    }
  }

  Snapshot Build() {
    if (root_) {
      if (!options_.full) {
        WalkPopups();
      }
      Walk(root_, 0);
    }
    Snapshot snapshot;
    snapshot.text = std::move(out_);
    if (truncated_) {
      snapshot.text += "... (more; the page is long)\n";
    }
    snapshot.refs.assign(refs_.begin(), refs_.end());
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
        const std::string name = Collapse(AxString(by, "name"));
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
    stack.push_back({start, start_depth, Step::kNode, std::string()});
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
      Step::Kind end = Step::kNode;
      const bool descend = options_.full ? Full(node, step.depth, &child_depth)
                                         : Compact(node, step.depth,
                                                   &child_depth, &within, &end);
      if (!descend) {
        continue;
      }
      if (end != Step::kNode) {
        stack.push_back({nullptr, step.depth, end, std::string()});
      }
      const base::ListValue* children = node.FindList("childIds");
      if (!children) {
        continue;
      }
      for (const base::Value& child : base::Reversed(*children)) {
        const std::string* child_id = child.GetIfString();
        auto it = child_id ? by_id_.find(*child_id) : by_id_.end();
        if (it != by_id_.end()) {
          stack.push_back({it->second, child_depth, Step::kNode, within});
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
               Step::Kind* end) {
    if (node.FindBool("ignored").value_or(false)) {
      return true;
    }
    const std::string role = AxString(node, "role");
    if (role == "InlineTextBox" || role == "ListMarker") {
      return false;
    }
    if (role == "StaticText" || role == "LineBreak") {
      // A line break's name is "\n": a space between the runs around it.
      const std::string text = AxString(node, "name");
      const std::string collapsed = Collapse(text);
      if (!collapsed.empty() && !within->empty() &&
          within->find(collapsed) != std::string::npos) {
        return false;
      }
      AddText(depth, text);
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
    const bool shown = action || landmark ||
                       ((!name.empty() || !value.empty()) && !wrapper &&
                        !inline_role && !kBlockRoles.contains(role));
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
    const std::string collapsed_name = Collapse(name);
    if (!collapsed_name.empty() && Collapse(pending_) == collapsed_name) {
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
    return true;
  }

  void AddText(int depth, std::string_view text) {
    if (!pending_.empty() && depth != pending_depth_) {
      Flush();
    }
    if (pending_.empty()) {
      pending_depth_ = depth;
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
    const std::string text = Collapse(pending_);
    pending_.clear();
    word_break_ = false;
    // A label after its control ("checkbox "Agree"" and then "Agree").
    if (text.empty() || text == last_name_) {
      return;
    }
    if (!Emit(pending_depth_, Quoted(text, kMaxText))) {
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
      line += " " + Quoted(options_.full ? name : Collapse(name), kMaxName);
    }
    if (!value.empty()) {
      line += " = " + Quoted(value, kMaxName);
    }
    if (!states.empty()) {
      line += " " + states;
    }
    if (with_ref) {
      if (std::optional<int> backend = node.FindInt("backendDOMNodeId")) {
        line += " @" + base::NumberToString(*backend);
      }
    }
    Emit(depth, line);
  }

  bool Emit(int depth, const std::string& line) {
    if (lines_ == options_.max_lines) {
      truncated_ = true;
      return false;
    }
    out_.append(static_cast<size_t>(depth) * 2, ' ');
    out_ += line;
    out_ += '\n';
    ++lines_;
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
  std::string out_;
  size_t lines_ = 0;
  bool truncated_ = false;
  // Text runs not written yet, and their depth.
  std::string pending_;
  int pending_depth_ = 0;
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
  }
  return "unknown";
}

std::optional<RefVerdict> RefVerdictFromName(std::string_view name) {
  for (RefVerdict verdict :
       {RefVerdict::kOk, RefVerdict::kNoSnapshot, RefVerdict::kOtherDocument,
        RefVerdict::kUnknown}) {
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
  if (page.document != document) {
    page.document = document;
    page.refs.clear();
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
  if (document.empty() || it->second.document != document) {
    return RefVerdict::kOtherDocument;
  }
  return it->second.refs.contains(ref) ? RefVerdict::kOk : RefVerdict::kUnknown;
}

void RefBook::Forget(const std::string& target) {
  pages_.erase(target);
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
    case RefVerdict::kOk:
      break;
  }
  return std::string();
}

}  // namespace koan::cli
