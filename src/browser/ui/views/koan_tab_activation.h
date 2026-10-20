// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_ACTIVATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_ACTIVATION_H_

#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class TabStripModel;

namespace koan {

// Tells koan_tab_hooks which tabs of a window the user has actually looked
// at, for Zen's "switch to the most recently used tab on close": a tab
// opened in the background counts once it is activated, not when created.
class KoanTabActivationTracker : public TabStripModelObserver {
 public:
  explicit KoanTabActivationTracker(TabStripModel* model);
  KoanTabActivationTracker(const KoanTabActivationTracker&) = delete;
  KoanTabActivationTracker& operator=(const KoanTabActivationTracker&) = delete;
  ~KoanTabActivationTracker() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_ACTIVATION_H_
