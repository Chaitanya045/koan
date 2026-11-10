// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSidebarNotification.mjs, ZenUpdates.mjs,
//   zen-sidebar-notification.css, zen-general.ftl
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// Zen's sidebar notification: its sizes, timing and the update strings.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_SIDEBAR_NOTIFICATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_SIDEBAR_NOTIFICATION_H_

#include "base/time/time.h"

namespace koan {

// zen-sidebar-notification.css.
inline constexpr int kSidebarNotificationFontSize = 12;
inline constexpr int kSidebarNotificationHeaderPadding = 16;
// The heading leaves room for the close button on both sides.
inline constexpr int kSidebarNotificationHeadingInset = 16 + 20;
inline constexpr int kSidebarNotificationCloseInset = 2;
inline constexpr int kSidebarNotificationCloseSize = 26;
inline constexpr int kSidebarNotificationCloseIconSize = 14;
inline constexpr int kSidebarNotificationCloseRadius = 4;
inline constexpr float kSidebarNotificationCloseOpacity = 0.4f;
inline constexpr float kSidebarNotificationCloseHoverOpacity = 0.15f;
inline constexpr float kSidebarNotificationBorderOpacity = 0.1f;
inline constexpr int kSidebarNotificationBodyPadding = 6;
inline constexpr int kSidebarNotificationLinkGap = 2;
inline constexpr int kSidebarNotificationLinkPadding = 4;
inline constexpr int kSidebarNotificationLinkIconSize = 16;
inline constexpr int kSidebarNotificationLinkIconGap = 6;
inline constexpr float kSidebarNotificationLinkOpacity = 0.7f;
inline constexpr base::TimeDelta kSidebarNotificationLinkFade =
    base::Milliseconds(200);

// ZenSidebarNotification.mjs: in after a second, 50px up and faded in; out
// 10px down and faded out (motion's default 0.3 s).
inline constexpr base::TimeDelta kSidebarNotificationInDelay = base::Seconds(1);
inline constexpr base::TimeDelta kSidebarNotificationAnimation =
    base::Milliseconds(300);
inline constexpr int kSidebarNotificationInOffset = 50;
inline constexpr int kSidebarNotificationOutOffset = 10;

// ZenUpdates.mjs: ZEN_UPDATE_NOTIFICATION_TIMEOUT_MS.
inline constexpr base::TimeDelta kUpdateNotificationTimeout = base::Seconds(15);

// zen-general.ftl.
inline constexpr char16_t kUpdatedHeading[] = u"Update Complete!";
inline constexpr char16_t kUpdatedLabel[] = u"What's new in Koan";
inline constexpr char16_t kUpdatedTooltip[] = u"View Release Notes";
inline constexpr char16_t kSomethingBrokeLabel[] = u"Something broke?";

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_SIDEBAR_NOTIFICATION_H_
