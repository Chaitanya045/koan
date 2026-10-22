// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_TAB_HOOKS_H_
#define KOAN_BROWSER_KOAN_TAB_HOOKS_H_

#include <optional>

#include "base/functional/function_ref.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

// Zen's tab behaviours that need a say in upstream tab code. Called from
// chrome/browser/ui/tabs/tab_strip_model.cc and
// chrome/browser/ui/tabs/back_to_opener/back_to_opener_controller.cc; kept
// free of tab strip types so those targets can depend on this one.
namespace koan {

// Records that `contents` became the active tab: "used" for
// PickRecentlyUsedTab(). Called by each window's KoanTabActivationTracker.
void OnTabActivated(content::WebContents* contents);

// The tab to select when the active tab closes and its opener doesn't
// decide: the most recently activated of the tabs `contents_at` returns (null
// for tabs that can't be selected), if prefs::kSelectRecentlyUsedOnClose is
// on. Tabs never activated (e.g. opened in the background) don't count;
// without any, upstream decides.
std::optional<int> PickRecentlyUsedTab(
    content::BrowserContext* context,
    int count,
    base::FunctionRef<content::WebContents*(int)> contents_at);

// Whether Back in a tab with no history may close it and return to its
// opener (upstream's back-to-opener, prefs::kCloseOnBackWithNoHistory).
// Never for a pinned tab, Essentials and tabs pinned to their workspace
// alike.
bool ShouldCloseTabOnBack(content::WebContents* contents);

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_TAB_HOOKS_H_
