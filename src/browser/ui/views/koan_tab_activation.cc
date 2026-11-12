// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_tab_activation.h"

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_tab_hooks.h"

namespace koan {

KoanTabActivationTracker::KoanTabActivationTracker(TabStripModel* model) {
  model->AddObserver(this);
  if (content::WebContents* active = model->GetActiveWebContents()) {
    OnTabActivated(active);
  }
}

KoanTabActivationTracker::~KoanTabActivationTracker() = default;

void KoanTabActivationTracker::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed() && selection.new_contents) {
    OnTabActivated(selection.new_contents);
  }
  if (selection.active_tab_changed() && selection.old_contents) {
    OnTabLeft(selection.old_contents);
  }
  // Opened in the background: no owner, as in Zen.
  if (change.type() == TabStripModelChange::kInserted) {
    for (const TabStripModelChange::ContentsWithIndex& inserted :
         change.GetInsert()->contents) {
      if (inserted.contents != tab_strip_model->GetActiveWebContents()) {
        OnTabLeft(inserted.contents);
      }
    }
  }
}

}  // namespace koan
