// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_VERTICAL_TABS_H_
#define KOAN_BROWSER_KOAN_VERTICAL_TABS_H_

class PrefService;

// Koan owns the sidebar: upstream's vertical tab strip stays, but its state
// machine (VerticalTabStripStateControllerImpl) answers to Koan. Called from
// chrome/browser/ui/tabs/vertical_tab_strip_state_controller_impl.cc, so kept
// free of tab strip types. See .agent/notes/vertical-tabs-audit.md.
namespace koan {

// Whether Koan decides the vertical tab strip's state in the windows that have
// the controller (normal windows): vertical tabs are always on, expand-on-hover
// is always on, and the strip is "collapsed" only while compact mode hides the
// sidebar; that is never saved to the profile or the session. Without Koan's
// sidebar (the feature is off) upstream behaves as in Chrome.
bool ForcesVerticalTabs();

// Whether a new window's sidebar starts out collapsed (hidden by compact
// mode), from `prefs`, so that a compact window starts hidden without an
// animation and an old saved collapse can't make a rail.
bool InitialSidebarCollapsed(const PrefService* prefs);

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_VERTICAL_TABS_H_
