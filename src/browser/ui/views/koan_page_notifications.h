// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_PAGE_NOTIFICATIONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_PAGE_NOTIFICATIONS_H_

#include <string>

class Profile;
struct NavigateParams;

namespace content {
class SiteInstance;
class WebContents;
}  // namespace content

namespace message_center {
class Notification;
}  // namespace message_center

// Notifications in Koan's Spaces (.agent/notes/upstream/space-containers.md,
// "Notifications"). A page's own notifications (`new Notification()`,
// Chromium's non-persistent ones, #478): a page in a Space's profile may show
// them like any tab; the page is the one content's NotificationEventDispatcher
// listens to for the notification's events. A service worker's
// (`registration.showNotification()`, persistent ones, #530): content keeps
// the profile's partition in the notification's id, so its events reach that
// profile's service worker (koan/browser/containers/koan_partition_domain.h);
// here they name a Space, and the worker's windows open in one. Push stays
// refused in a Space's profile.
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

// PlatformNotificationServiceImpl::DisplayPersistentNotification(), before it
// shows `notification`, a service worker's notification of `profile`: for one
// in a Space's profile (its id names a user container), the line under the
// title names a Space with that profile after the site, as a page's does: the
// Space of a tab of the site in that profile, else the active Space if it has
// the profile, else the first that has it; with none, the profile's name.
// Upstream's otherwise.
void OnShowingServiceWorkerNotification(
    Profile* profile,
    message_center::Notification& notification);

// PersistentNotificationHandler::OnClick() and OnClose(), before content
// dispatches the event: loads the partition of the Space's profile that the
// service worker's notification `notification_id` names, if that profile is
// listed (prefs::kUserContainers). Content looks the partition up without
// creating it (NotificationEventDispatcherImpl::DispatchNotificationEvent()),
// so an event of a profile that no longer exists is dropped and doesn't
// create its folder again, while one of a listed profile reaches it also
// before anything else has loaded it (a click after a restart, on macOS).
void LoadNotificationPartition(Profile* profile,
                               const std::string& notification_id);

// ChromeContentBrowserClient::OpenURL(), before it navigates `params`: a
// service worker's window (`clients.openWindow()`, also a payment handler's)
// from `worker`, its SiteInstance. For a worker in a Space's profile the tab
// opens in that profile, in a Space that has it (the one
// OnShowingServiceWorkerNotification() names), which then shows; with no
// such Space, in the profile in the Space it opens in. Upstream's (the
// Space's profile) otherwise.
void PlaceServiceWorkerWindow(content::SiteInstance* worker,
                              NavigateParams& params);

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
