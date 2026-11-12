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
  size_t max_lines = 1500;
};

struct Snapshot {
  std::string text;
  // Every backend DOM node id in the tree, whether its line shows a ref or
  // not: a ref is the node's id in both outlines, so a ref from --full works
  // after a compact snapshot of the same page, and the other way round.
  std::vector<int> refs;
};

// The outline: one line per node, indented by depth,
//   role "name" = "value" [states] @ref
// Compact (the default) leaves out inline text boxes and line breaks, joins
// the text runs of a block into one line, drops text that only repeats the
// name of the node it is in, flattens unnamed containers, and shows a ref
// only on what an agent acts on or finds its way by (controls, links,
// focusable and editable nodes, landmarks, headings, frames).
Snapshot BuildSnapshot(const base::DictValue& ax_tree,
                       const SnapshotOptions& options);

// A node's states from its AX properties, as the outline shows them:
// "[checked, required]", or "" if it has none worth showing. Only what is
// set: checked, mixed, pressed, selected, expanded / collapsed, disabled,
// required, readonly, invalid, focused, modal, and a heading's level.
std::string StatesText(const base::DictValue& ax_node);

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

 private:
  struct Page {
    std::string document;
    std::set<int> refs;
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
