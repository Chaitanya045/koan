// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_PALETTE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_PALETTE_H_

#include <string_view>

#include "base/memory/raw_ptr.h"
#include "koan/browser/ui/views/koan_command_registry.h"
#include "ui/events/event_handler.h"

class OmniboxViewViews;

namespace koan {

class KoanBrowserViewController;

// Typed first in the URL bar, lists every action (Zen's "Actions" search
// mode). Tab in an empty URL bar types it.
inline constexpr char16_t kActionsModePrefix = u'>';

// Zen's command palette for one window: its actions are suggestions of the
// URL bar (an AutocompleteProvider, see koan_command_palette.cc), so it
// works with any look of the URL bar.
// - Typing three or more characters suggests up to five matching actions
//   right below the first suggestion; a fuzzy match on the label, like Zen.
// - ">" (or Tab in an empty URL bar) lists every action; Enter runs the
//   first match.
// - Actions the user runs move to the top next time, and Enter runs them;
//   actions shown but passed over move down (Zen's results learner).
// The actions come from `registry()`; each feature adds its own.
class KoanCommandPalette : public ui::EventHandler {
 public:
  explicit KoanCommandPalette(KoanBrowserViewController* controller);
  KoanCommandPalette(const KoanCommandPalette&) = delete;
  KoanCommandPalette& operator=(const KoanCommandPalette&) = delete;
  ~KoanCommandPalette() override;

  KoanCommandRegistry& registry() { return registry_; }

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // ui::EventHandler (the omnibox's pre-target handler):
  void OnKeyEvent(ui::KeyEvent* event) override;

  // Zen's fuzzy score of `query` against `target`, both lower case: exact
  // and prefix matches win, then matches at word starts and in runs. 0 for
  // no match.
  static int FuzzyScore(std::u16string_view target, std::u16string_view query);

 private:
  const raw_ptr<KoanBrowserViewController> controller_;
  KoanCommandRegistry registry_;
  raw_ptr<OmniboxViewViews> omnibox_ = nullptr;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMMAND_PALETTE_H_
