// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_PAGE_NOTIFICATIONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_PAGE_NOTIFICATIONS_H_

#include <string>

class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace message_center {
class Notification;
}  // namespace message_center

// A page's own notifications (`new Notification()`, Chromium's
// non-persistent ones) in Koan's Spaces (#478,
// .agent/notes/upstream/space-containers.md, "Notifications"). A page in a
// Space's profile may show them like any tab; service-worker notifications
// and push stay refused there. The page is the one content's
// NotificationEventDispatcher listens to for the notification's events.
namespace koan {

// PlatformNotificationServiceImpl::DisplayNotification(), before it shows
// `notification`, a page's notification of `profile`: remembers its tab, and
// the window and Space the tab is in, for a click. For a page in a Space's
// profile (a user container), the line under the title names the Space after
// the site ("example.com · Work"), so two accounts of one site can be told
// apart. Notifications from other pages are left as upstream makes them.
void OnShowingPageNotification(Profile* profile,
                               message_center::Notification& notification);

// NonPersistentNotificationHandler::OnClick(), before the page gets the
// click: shows the notification's tab, its Space and its window, as Firefox
// does (Chromium leaves it to the page's click handler). If the tab has closed
// since, shows the Space it was in, so the tab upstream then opens for the
// site opens in that Space and its profile.
void OnPageNotificationClicked(Profile* profile,
                               const std::string& notification_id);

// The line under a page notification's title for a page in a Space's
// profile: the site (as Chromium shows it), then the Space.
std::u16string PageNotificationContext(const std::u16string& site,
                                       const std::u16string& space);

// Whether the site panel of `contents` says that push notifications aren't
// available in its Space's profile: a web page in a Space's profile whose
// site isn't blocked from notifying.
bool ShowsProfileNotificationNote(content::WebContents* contents);

// The site panel's note.
inline constexpr char16_t kProfileNotificationNote[] =
    u"Push notifications aren't available in this Space's profile. The site "
    u"can notify you while its tab is open.";

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_PAGE_NOTIFICATIONS_H_
