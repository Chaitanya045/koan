// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_SNAPSHOT_H_
#define KOAN_CLI_KOAN_CLI_SNAPSHOT_H_

#include <stddef.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

// `koan snapshot`: the page's accessibility tree (Accessibility.getFullAXTree)
// as an outline an agent can read, and the refs it may act on afterwards
// (.agent/notes/agent-control.md sections 12 and 20.3). Pure: no I/O.
namespace koan::cli {

struct SnapshotOptions {
  // Every node the tree shows (inline text boxes, line breaks, each text
  // run, a ref on every node), instead of the compact outline.
  bool full = false;
  // Only what an agent acts on (controls, links, focusable and editable
  // nodes, with their names, values and states), the headings and dialogs
  // around them, and the text of alerts and status messages. A closed
  // dropdown's options are left out (its value is on its line; `choose`
  // finds an option by its text).
  bool interactive = false;
  // Only the subtree of the node with this backend DOM node id (0: the
  // whole page). Open popups are not put first then.
  int root = 0;
  // After each ref, a locator (loc:role=...[name="..."]) when no other
  // node of the tree has that role and name.
  bool locators = false;
  size_t max_lines = 1500;
};

// One line of the outline.
struct SnapshotLine {
  // The backend DOM node id of what the line shows (a text line: its first
  // run's), 0 if none (a popup's heading).
  int node = 0;
  int depth = 0;
  // Without the indent.
  std::string text;
  // Shown whatever the scope: the page's own line and open popups.
  bool pinned = false;
};

struct Snapshot {
  std::string text;
  // Every backend DOM node id in the tree, whether its line shows a ref or
  // not: a ref is the node's id in both outlines, so a ref from --full works
  // after a compact snapshot of the same page, and the other way round.
  std::vector<int> refs;
  std::vector<SnapshotLine> lines;
  // The outline stopped at max_lines.
  bool truncated = false;
  // `root` is not a node of the tree.
  bool root_missing = false;
};

// Whitespace runs as one space, none at the ends.
std::string CollapseSpace(std::string_view text);

Snapshot BuildSnapshot(const base::DictValue& ax_tree,
                       const SnapshotOptions& options);

// A node's states from its AX properties, as the outline shows them:
// "[checked, required]", or "" if it has none worth showing. Only what is
// set: checked, mixed, pressed, selected, expanded / collapsed, disabled,
// required, readonly, invalid, focused, modal, and a heading's level.
std::string StatesText(const base::DictValue& ax_node);

// The outline cut to lines [first, end) of `snapshot.lines` (the viewport,
// ViewportSearch), with its pinned lines wherever they are, and "[more
// above: N nodes]" and "[more below: N nodes]" for what was cut.
std::string RenderWindow(const Snapshot& snapshot, size_t first, size_t end);

// --- Diffs: what changed since the last snapshot ----------------------------

// What the holder keeps of a tab's last snapshot, to compare the next one
// with: its document (a new one resets the baseline), what kind of outline
// it was (compact, interactive, full, and the scope root), and its lines.
struct Baseline {
  std::string document;
  std::string kind;
  std::vector<SnapshotLine> lines;
};
// The kind of outline `options` gives, for Baseline::kind.
std::string BaselineKind(const SnapshotOptions& options);
base::DictValue BaselineToValue(const Baseline& baseline);
std::optional<Baseline> BaselineFromValue(const base::DictValue& value);

// The holder's baselines: the last snapshot of each tab, of this client.
class Baselines {
 public:
  Baselines();
  Baselines(const Baselines&) = delete;
  Baselines& operator=(const Baselines&) = delete;
  ~Baselines();

  // Keeps `next` for `target`, and returns the one it replaces.
  std::optional<Baseline> Swap(const std::string& target, Baseline next);

 private:
  std::map<std::string, Baseline> pages_;
};

// What `--diff` prints: the lines added ("+ "), changed ("~ ", as they are
// now) and removed ("- ", as they were) since `previous`, under a count; or,
// when there is nothing to compare with (no snapshot before, another page,
// another kind of outline), "baseline reset (why)" and `full_text`, the
// whole outline.
std::string DiffText(const std::optional<Baseline>& previous,
                     const Baseline& now,
                     std::string_view full_text);

// --- Refs: which page a ref belongs to ---------------------------------------

enum class RefVerdict {
  kOk,
  // No snapshot of this tab since the connection started.
  kNoSnapshot,
  // The tab shows another document than the one the snapshot read (it
  // navigated, reloaded, or the page replaced itself).
  kOtherDocument,
  // Not a node of any snapshot of this document.
  kUnknown,
  // From before the user had the tab (it was paused and has resumed since,
  // design section 9.2): they may have changed the page.
  kResumed,
};

std::string_view RefVerdictName(RefVerdict verdict);
std::optional<RefVerdict> RefVerdictFromName(std::string_view name);

// What the holder remembers per tab: the document its last snapshot read
// (the main frame's loader id, new for every document) and the refs of
// every snapshot of that document. A ref is acted on only if it is one of
// them and the tab still shows that document: refs are backend DOM node ids,
// which a new renderer process numbers from 1 again, so a ref from the page
// before a cross-site navigation would name some node of the new page.
class RefBook {
 public:
  RefBook();
  RefBook(const RefBook&) = delete;
  RefBook& operator=(const RefBook&) = delete;
  ~RefBook();

  // A snapshot of `target` showing `document` with `refs`. Another document
  // than the last one replaces what was known about the tab.
  void Remember(const std::string& target,
                const std::string& document,
                const std::vector<int>& refs);
  RefVerdict Check(const std::string& target,
                   const std::string& document,
                   int ref) const;
  void Forget(const std::string& target);
  // The tab came back from a pause (Koan.resumed): every ref of it is stale
  // (kResumed) until its next snapshot.
  void Resumed(const std::string& target);

 private:
  struct Page {
    std::string document;
    std::set<int> refs;
    bool resumed = false;
  };
  std::map<std::string, Page> pages_;
};

// Why a ref is refused, with what to do (verdict other than kOk). `tab` is
// what the agent typed, for the command it should run.
std::string RefRefusal(RefVerdict verdict,
                       std::string_view ref,
                       std::string_view tab);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_SNAPSHOT_H_
