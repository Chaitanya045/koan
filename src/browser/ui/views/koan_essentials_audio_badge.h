// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_AUDIO_BADGE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_AUDIO_BADGE_H_

class TabView;

namespace koan {

// Zen's audio badge on an Essentials tile (koan_essentials_audio.h): a round
// button over the favicon's top-right corner while the tab plays audio or is
// muted; clicking it toggles the tab's mute, as Zen's .tab-icon-overlay does,
// without selecting the tab. Upstream's alert indicator, which would show
// instead of the favicon, stays hidden then (HidesTabAlertIndicator() in
// koan_essentials.h).

// Adds the (hidden) badge to a new vertical tab view. Called from
// OnTabViewCreated(), after the tab view's own children.
void AddEssentialsAudioBadge(TabView* tab_view);

// Brings the tab view's badge up to date with the tab (pinned, active).
// Called when the tab view updates its colours (TabView::UpdateColors(), from
// its state, hover and theme changes).
void UpdateEssentialsAudioBadge(const TabView* tab_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_AUDIO_BADGE_H_
