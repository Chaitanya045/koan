// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_EXTENSION_EVENTS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_EXTENSION_EVENTS_H_

namespace content {
class WebContents;
}

namespace extensions {
struct Event;
}

// Extensions per Space profile (#473, koan_container_extensions.h): the events
// about a tab that an extension which is off in the tab's profile doesn't get.
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

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_EXTENSION_EVENTS_H_
