// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_vertical_tabs_policy.h"

#include <string>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_vertical_tabs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_sidebar_header.h"
#include "ui/actions/actions.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// Upstream's prefs for the strip's mode and hover; the collapse and the width
// of the last window are the others (the width is kept).
constexpr const char* const kOldPrefs[] = {
    ::prefs::kVerticalTabsEnabled,
    ::prefs::kVerticalTabsExpandOnHoverEnabled,
    ::prefs::kVerticalTabsCollapsedState,
};

#if DCHECK_IS_ON()
void OnModeWillChange(tabs::VerticalTabStripStateController*) {
  DLOG(FATAL) << "Koan: something changed the vertical tab strip's mode; "
                 "Koan's sidebar stays vertical. See "
                 "koan_vertical_tabs_policy.h.";
}
#endif

}  // namespace

KoanVerticalTabsPolicy::KoanVerticalTabsPolicy(BrowserView* browser_view)
    : browser_view_(browser_view) {}

KoanVerticalTabsPolicy::~KoanVerticalTabsPolicy() = default;

PrefService* KoanVerticalTabsPolicy::GetPrefs() const {
  // Off the record windows share the profile's layout.
  return browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs();
}

void KoanVerticalTabsPolicy::OnAddedToWidget() {
  // (Once: nothing here may be installed twice.)
  if (added_ || !OwnsSidebar() || !browser_view_->GetIsNormalType()) {
    return;
  }
  added_ = true;
  RemoveOldPrefs();
  InstallActions();
  StartWatching();
}

void KoanVerticalTabsPolicy::OnBrowserViewDestroying() {
  // First: from here on, nothing here (nor in compact mode) touches the
  // strip. Closing a window tears the browser's animation controller down
  // before the BrowserView, and a collapse or expand needs it.
  destroying_ = true;
  weak_factory_.InvalidateWeakPtrs();
  pref_change_registrar_.RemoveAll();
  mode_subscription_ = {};
  collapse_subscription_ = {};
}

bool KoanVerticalTabsPolicy::IsClosing() const {
  // A closing window is torn down by the browser's own tasks: the widget is
  // closed first, and the browser's animation controller is destroyed before
  // the BrowserView (so before destroying_ is set). Starting a collapse or an
  // expand animation in between is the #130 crash.
  const BrowserWidget* widget = browser_view_->browser_widget();
  return destroying_ || !widget || widget->IsClosed() ||
         browser_view_->browser()->IsDeleteScheduled();
}

void KoanVerticalTabsPolicy::SetSidebarHidden(bool hidden) {
  if (IsClosing()) {
    return;
  }
  auto* state =
      tabs::VerticalTabStripStateController::From(browser_view_->browser());
  if (!state) {
    return;
  }
  hidden_ = hidden;
  // Collapsing or collapsed counts as hidden: IsCollapsed() only becomes true
  // when the collapse animation ends, and a request to expand in the meantime
  // (compact toggled twice quickly) would be skipped, leaving the icon rail.
  const bool collapsed = state->GetCollapseState() !=
                         tabs::VerticalTabStripCollapseState::kExpanded;
  if (collapsed != hidden) {
    settling_ = true;
    ScopedSidebarCollapseRequest request;
    state->RequestCollapse(hidden);
  }
}

actions::ActionItem* KoanVerticalTabsPolicy::FindAction(int action_id) const {
  BrowserActions* actions = BrowserActions::From(browser_view_->browser());
  if (!actions || !actions->root_action_item()) {
    return nullptr;
  }
  return actions::ActionManager::Get().FindAction(action_id,
                                                  actions->root_action_item());
}

void KoanVerticalTabsPolicy::InstallActions() {
  // Commands that would change the strip's mode or hover: nothing, and not
  // offered (an action that is not visible has no menu item in the glow-up
  // menus; one that is not enabled is greyed in the others).
  for (int action_id :
       {kActionToggleVerticalTabs, kActionToggleVerticalTabsExpandOnHover,
        kActionVerticalTabsSendFeedback}) {
    if (actions::ActionItem* item = FindAction(action_id)) {
      item->SetInvokeActionCallback(base::DoNothing());
      item->SetVisible(false);
      item->SetEnabled(false);
    }
  }

  // The sidebar button (and its menu item and Shift+Cmd+L): in Zen the
  // sidebar button toggles compact mode. Chromium's collapse to an icon rail,
  // with its tab group and tab search buttons and its toolbar, is not
  // something Zen has.
  if (actions::ActionItem* toggle = FindAction(kActionToggleCollapseVertical)) {
    toggle->SetInvokeActionCallback(base::BindRepeating(
        [](base::WeakPtr<KoanVerticalTabsPolicy> policy,
           actions::ActionItem* item, actions::ActionInvocationContext) {
          if (policy) {
            policy->OnSidebarButtonAction();
          }
        },
        weak_factory_.GetWeakPtr()));
    // Shift+Cmd+L is Koan's inspector on macOS (Zen's key_inspectorMac, see
    // koan_shortcut_table.cc), not the sidebar button's: the action keeps no
    // key, so it can't compete with it (the compact mode key is Cmd+S).
    toggle->SetAccelerator(ui::Accelerator());
  }
  UpdateSidebarButtonText();

  // Shift+Cmd+A: Chromium's tab search bubble hangs off the header's tab
  // search button, which Koan removes. Koan's URL bar is where tabs are
  // searched.
  if (actions::ActionItem* search = FindAction(kActionTabSearch)) {
    search->SetInvokeActionCallback(base::BindRepeating(
        [](base::WeakPtr<KoanVerticalTabsPolicy> policy,
           actions::ActionItem* item, actions::ActionInvocationContext) {
          if (policy) {
            policy->OnTabSearchAction();
          }
        },
        weak_factory_.GetWeakPtr()));
  }
}

void KoanVerticalTabsPolicy::OnSidebarButtonAction() {
  if (!features::IsCompactModeEnabled()) {
    return;
  }
  // Also in fullscreen, where compact mode doesn't show but its setting still
  // toggles, as with the shortcut.
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view_)) {
    controller->compact_mode()->ToggleCompactMode();
  }
}

void KoanVerticalTabsPolicy::OnTabSearchAction() {
  chrome::ExecuteCommand(browser_view_->browser(), IDC_FOCUS_LOCATION);
}

void KoanVerticalTabsPolicy::UpdateSidebarButtonText() {
  if (destroying_ || !features::IsCompactModeEnabled()) {
    return;
  }
  actions::ActionItem* toggle = FindAction(kActionToggleCollapseVertical);
  if (!toggle) {
    return;
  }
  // Upstream rewrites these ("Collapse tabs") on every collapse change; this
  // runs after that (from compact mode's UpdateActive(), a posted task).
  const std::u16string text = GetPrefs()->GetBoolean(prefs::kCompactModeEnabled)
                                  ? u"Disable compact mode"
                                  : u"Enable compact mode";
  toggle->SetText(text);
  toggle->SetTooltipText(text);
}

void KoanVerticalTabsPolicy::RemoveOldPrefs() {
  PrefService* prefs = GetPrefs();
  pref_change_registrar_.Init(prefs);
  for (const char* pref : kOldPrefs) {
    pref_change_registrar_.Add(
        pref, base::BindRepeating(
                  [](base::WeakPtr<KoanVerticalTabsPolicy> policy,
                     const std::string& path) {
                    // (Clearing a pref that is already at its default does not
                    // notify.)
                    if (policy) {
                      policy->GetPrefs()->ClearPref(path);
                    }
                  },
                  weak_factory_.GetWeakPtr()));
    if (prefs->GetUserPrefValue(pref)) {
      prefs->ClearPref(pref);
    }
  }
}

void KoanVerticalTabsPolicy::StartWatching() {
  auto* state =
      tabs::VerticalTabStripStateController::From(browser_view_->browser());
  if (!state) {
    return;
  }
  // (A new window's sidebar starts hidden when compact mode hides it, see
  // koan::InitialSidebarCollapsed().)
  hidden_ = state->IsCollapsed();
#if DCHECK_IS_ON()
  mode_subscription_ =
      state->RegisterOnModeWillChange(base::BindRepeating(&OnModeWillChange));
#endif
  collapse_subscription_ = state->RegisterOnCollapseChanged(base::BindRepeating(
      &KoanVerticalTabsPolicy::OnCollapseChanged, base::Unretained(this)));
}

void KoanVerticalTabsPolicy::OnCollapseChanged(
    tabs::VerticalTabStripCollapseState state) {
  if (IsClosing()) {
    return;
  }
  const bool settled =
      state != tabs::VerticalTabStripCollapseState::kCollapsing;
  if (settled) {
    if ((state == tabs::VerticalTabStripCollapseState::kCollapsed) == hidden_) {
      settling_ = false;
#if DCHECK_IS_ON()
    } else if (!settling_) {
      // A request that is taken back before its animation ends goes through
      // the other state on the way (settling_); anything else is a leak.
      DLOG(FATAL) << "Koan: the vertical tab strip was collapsed or expanded "
                     "by something other than KoanVerticalTabsPolicy.";
#endif
    }
  }
  // Upstream rewrites the sidebar button's text on every collapse change
  // ("Collapse tabs"), after this notification: put Koan's back after it.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&KoanVerticalTabsPolicy::UpdateSidebarButtonText,
                     weak_factory_.GetWeakPtr()));
}

bool ShowSidebarButtonContextMenu(BrowserWindowInterface* browser,
                                  const gfx::Point& screen_point,
                                  ui::mojom::MenuSourceType source_type) {
  if (!OwnsSidebar()) {
    return false;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  if (!controller) {
    return false;
  }
  controller->compact_mode()->ShowContextMenu(screen_point, source_type);
  return true;
}

}  // namespace koan
