// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_EXTENSION_EVENTS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_EXTENSION_EVENTS_H_

#include <list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/auto_reset.h"
#include "base/functional/callback.h"
#include "base/memory/stack_allocated.h"
#include "base/values.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace download {
class DownloadItem;
}

namespace extensions {
struct Event;
}

namespace sessions::tab_restore {
struct Entry;
}

// Extensions per Space profile (#473, koan_container_extensions.h): the events
// about a tab, a visit or a download that an extension which is off in its
// Space profile doesn't get, and the downloads and recently closed tabs it
// can't see (#544).
namespace koan::containers {

// Makes `event`, about `contents` (a tab, or a frame in it), skip every
// extension that is off in the tab's Space profile. Its own
// `will_dispatch_callback`, if it has one, still runs for the others. Nothing
// changes for a tab without such extensions. Called by the webNavigation
// event helpers (web_navigation_api_helpers.cc) before the event is
// broadcast; the event router runs the callback per extension, synchronously,
// while the tab is alive, and the list it checks is taken now.
void SkipExtensionsOffIn(extensions::Event& event,
                         content::WebContents* contents);

// What the events built on this thread while it lives skip: set where an
// event's subject is known (a tab, a visit, a download), for the event router
// function that builds the API's events (TabsEventRouter::DispatchEvent(),
// HistoryEventRouter::DispatchEvent(),
// ExtensionDownloadsEventRouter::DispatchEvent()), which has no subject and
// calls SkipExtensionsInScope(). Those routers build and broadcast
// synchronously. Scopes nest; the innermost one applies.
class ScopedEventSkip {
  STACK_ALLOCATED();

 public:
  // Per extension, when the event is dispatched to it: false to skip it.
  // It may give the extension other arguments (`args_out`).
  using Filter =
      base::RepeatingCallback<bool(content::BrowserContext* context,
                                   std::string_view extension_id,
                                   std::optional<base::ListValue>& args_out)>;

  // Skips nothing until Add().
  ScopedEventSkip();
  // Skips the extensions that are off in `tab`'s profile.
  explicit ScopedEventSkip(content::WebContents* tab);
  // Skips `off` (extension ids).
  explicit ScopedEventSkip(std::set<std::string> off);
  // Asks `filter`.
  explicit ScopedEventSkip(Filter filter);
  ScopedEventSkip(const ScopedEventSkip&) = delete;
  ScopedEventSkip& operator=(const ScopedEventSkip&) = delete;
  ~ScopedEventSkip();

  // Also skips the extensions that are off in `tab`'s profile: an event about
  // several tabs (tabs.onHighlighted) skips an extension that is off in any.
  void Add(content::WebContents* tab);

  const std::set<std::string>& off() const { return off_; }
  const Filter& filter() const { return filter_; }

 private:
  std::set<std::string> off_;
  Filter filter_;
  base::AutoReset<const ScopedEventSkip*> reset_;
};

// Makes `event` skip what the innermost ScopedEventSkip says, taken now; its
// own `will_dispatch_callback` still runs for the others. Nothing outside a
// scope.
void SkipExtensionsInScope(extensions::Event& event);

// --- Downloads (chrome.downloads) ---

// Every extension that is off in the profile `download` was started in: its
// partition (DownloadItem::GetSerializedEmbedderDownloadData(), kept with the
// download and in the downloads database), and its tab's while that is open.
std::set<std::string> ExtensionsOffInDownload(
    const download::DownloadItem* download);

// Whether the extension whose function is running (ScopedExtensionCall) is off
// in `download`'s profile: the downloads functions' ShouldExport(), which
// every search, erase and id lookup asks, then don't see it. False outside a
// function (the downloads events, the browser's own lookups).
bool IsCallingExtensionOffInDownload(const download::DownloadItem& download);

// --- Recently closed tabs (chrome.sessions) ---

// Whether `extension_id` doesn't see `entry` of the recently closed list: a
// tab of a profile it is off in, or a window, group or split whose every tab
// is. SessionsGetRecentlyClosedFunction leaves such tabs and entries out, and
// SessionsRestoreFunction doesn't restore them.
bool HidesClosedEntry(content::BrowserContext* context,
                      std::string_view extension_id,
                      const sessions::tab_restore::Entry& entry);

// Whether `extension_id` doesn't see the entry, or the tab in an entry, that
// `id` names in `entries` (TabRestoreService::entries()): what
// sessions.restore(sessionId) refuses.
bool HidesClosedId(
    content::BrowserContext* context,
    std::string_view extension_id,
    const std::list<std::unique_ptr<sessions::tab_restore::Entry>>& entries,
    int id);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_EXTENSION_EVENTS_H_
