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

}  // namespace koan::switches

#endif  // KOAN_BROWSER_KOAN_SWITCHES_H_
