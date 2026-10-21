// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_REGISTRY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_REGISTRY_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"

namespace gfx {
struct VectorIcon;
}

namespace koan {

class KoanBrowserViewController;

// One action of the command palette (Zen's "global actions"): what the URL
// bar suggests when the typed text matches its label.
struct KoanCommand {
  using RunCallback = base::RepeatingCallback<void(KoanBrowserViewController&)>;

  KoanCommand();
  KoanCommand(const KoanCommand&);
  KoanCommand& operator=(const KoanCommand&);
  KoanCommand(KoanCommand&&);
  KoanCommand& operator=(KoanCommand&&);
  ~KoanCommand();

  // Stable identifier, e.g. "toggle-compact-mode". The palette remembers
  // which commands the user runs by it (Zen's results learner).
  std::string id;
  // Zen's label, e.g. "Toggle Compact Mode".
  std::u16string label;
  // The action's position in Zen's list (ZenUBGlobalActions.sys.mjs), which
  // orders the commands of all features as in Zen.
  int order = 0;
  // What the command applies to, e.g. a Space's name for "Focus on". Shown
  // beside the label and matched instead of it (Zen's "prettyName").
  std::u16string target;
  raw_ptr<const gfx::VectorIcon> icon = nullptr;
  // The keyboard shortcut that does the same, e.g. "Ctrl+S"; may be empty.
  std::u16string shortcut;
  RunCallback run;
};

// The commands of one window. Each feature adds a source that lists the
// commands available right now (e.g. "Unpin Tab" only for a pinned tab), so
// features add their own actions without touching the palette.
class KoanCommandRegistry {
 public:
  // Appends the commands that apply right now.
  using Source = base::RepeatingCallback<void(std::vector<KoanCommand>&)>;

  KoanCommandRegistry();
  KoanCommandRegistry(const KoanCommandRegistry&) = delete;
  KoanCommandRegistry& operator=(const KoanCommandRegistry&) = delete;
  ~KoanCommandRegistry();

  void AddSource(Source source);

  // Every source's available commands, by `order`.
  std::vector<KoanCommand> GetAvailableCommands() const;

 private:
  std::vector<Source> sources_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_REGISTRY_H_
