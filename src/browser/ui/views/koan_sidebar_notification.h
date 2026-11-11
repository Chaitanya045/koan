// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NOTIFICATION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NOTIFICATION_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/time/time.h"
#include "koan/browser/ui/views/koan_zen_sidebar_notification.h"

class BrowserView;

namespace gfx {
struct VectorIcon;
}

namespace koan {

// Zen's sidebar notification (ZenSidebarNotification.mjs,
// zen-sidebar-notification.css): a card at the bottom of the sidebar, under
// the media controls and above the Spaces' bottom bar, with a heading, a close
// button and a few link rows. It slides in a second after it is shown, and
// with `auto_hide` it closes by itself once its progress line has run out
// (held while the pointer is over it). Zen only shows it in an expanded
// sidebar. A link's click runs its action and closes the card.
struct SidebarNotificationLink {
  SidebarNotificationLink(const gfx::VectorIcon& icon,
                          std::u16string label,
                          std::u16string tooltip,
                          base::OnceClosure action,
                          bool special = false);
  SidebarNotificationLink(SidebarNotificationLink&&);
  SidebarNotificationLink& operator=(SidebarNotificationLink&&);
  ~SidebarNotificationLink();

  raw_ref<const gfx::VectorIcon> icon;
  std::u16string label;
  std::u16string tooltip;
  base::OnceClosure action;
  // Zen's [special] row: full opacity, in the accent colour.
  bool special = false;
};

struct SidebarNotificationParams {
  SidebarNotificationParams();
  SidebarNotificationParams(SidebarNotificationParams&&);
  SidebarNotificationParams& operator=(SidebarNotificationParams&&);
  ~SidebarNotificationParams();

  std::u16string heading;
  std::vector<SidebarNotificationLink> links;
  // Zero: stays until closed.
  base::TimeDelta auto_hide;
  // Before it slides in: Zen's second after the card is added. A caller that
  // has waited already passes less.
  base::TimeDelta delay = kSidebarNotificationInDelay;
};

// Shows a notification in `browser_view`'s sidebar. False (and nothing shown)
// without a sidebar with Spaces, or while it is collapsed to icons, as Zen
// (createSidebarNotification()).
bool ShowSidebarNotification(BrowserView* browser_view,
                             SidebarNotificationParams params);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NOTIFICATION_H_
