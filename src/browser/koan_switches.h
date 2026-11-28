// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_SWITCHES_H_
#define KOAN_BROWSER_KOAN_SWITCHES_H_

namespace koan::switches {

// The Glance trigger's renderer switch is in koan/common/koan_glance_trigger.h.

// For e2e tests of the per-window session data (koan_session_data.h): each
// window without test data gets a random value of its own under
// koan::kTestWindowDataKey, which a restored or reopened window keeps.
inline constexpr char kTestWindowData[] = "koan-test-window-data";

// For e2e tests of the icons (koan_icons.h; builds with DCHECKs only, not
// release): appends every icon the UI asks for, and whether it was swapped for
// a Koan icon, to this file.
inline constexpr char kIconTrace[] = "koan-icon-trace";
// For tests off macOS: koan::IsWindowGlassEnabled()
// is true in every window, so what paints the window's background draws it
// translucent (over the headless window's plain backdrop; there is no blur to
// see).
inline constexpr char kTestWindowGlass[] = "koan-test-window-glass";

// For tests of the import from other browsers (koan_import_sources.h): the
// folder that stands in for ~/Library/Application Support, where Koan looks
// for Zen's, Arc's, Chrome's... profiles, so a test never reads the user's
// own. With it, the browsers' Safe Storage keys come from files in its
// Keychain/ folder, never from the keychain.
inline constexpr char kImportTestRoot[] = "koan-import-test-root";

// For testing "Close tabs I haven't used for" (koan_tab_auto_close.h): tabs
// close after this many seconds unused instead of the setting's hours, and
// windows check more often. Only while the setting isn't Never.
inline constexpr char kAutoCloseTabsAfter[] = "koan-auto-close-tabs-after";

// For testing a proxy per Space (#445) before it has a UI:
// --koan-container-proxy="<profile name>=<value>" sets the proxy of the
// user's container (a Space's profile) with that name when the profile's first
// container tab loads. The value is a proxy server, e.g.
// socks5://127.0.0.1:1080 for `ssh -D 1080 host` (saved, as the setting would
// be), "none" (removes the setting) or "disconnected" (this run only: the
// container's tabs fail, as while a tunnel is down). ParseProxySwitch() in
// koan/browser/containers/koan_container_proxy.h.
inline constexpr char kContainerProxy[] = "koan-container-proxy";

}  // namespace koan::switches

#endif  // KOAN_BROWSER_KOAN_SWITCHES_H_
