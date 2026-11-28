// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_QUICK_WINDOW_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_QUICK_WINDOW_H_

#include <vector>

class BrowserWindowInterface;
class Profile;
struct StartupTab;

namespace base {
class CommandLine;
}

namespace koan {

// Hook at the start of StartupBrowserCreatorImpl::OpenTabsInBrowser(), the
// one place links from other apps pass through on every platform: macOS's
// application:openURLs: (and Handoff, and the links held until startup ends)
// through OpenStartupTabsInBrowserWithProfile(), Linux's process singleton
// (a second `koan <url>`, xdg-open) through
// StartupBrowserCreator::ProcessCommandLineAlreadyRunning() ->
// StartupBrowserCreatorImpl::Launch() -> RestoreOrCreateBrowser(). Both mark
// them with ScopedExternalLinks (koan_spaces.h).
//
// With "Links from other apps open in: a quick window"
// (prefs::kExternalLinksOpenIn), each of `tabs` that no Space route takes
// opens in a quick window of its own (Arc's Little Arc, #472;
// koan_quick_window_controller.h) and the others are left in `rest`, for the
// caller to open in `browser` as usual. If `browser` is itself a quick
// window (Linux hands the most recently active window), it becomes the most
// recent normal window, so routed links don't open a new one. Returns false,
// leaving `rest` and `browser` alone, when it changes nothing: then the
// caller goes on as upstream. `command_line` is the launch's (Linux: its XDG
// activation token goes to the quick window).
bool TakeQuickWindowLinks(const std::vector<StartupTab>& tabs,
                          const base::CommandLine& command_line,
                          Profile* profile,
                          BrowserWindowInterface*& browser,
                          std::vector<StartupTab>& rest);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_QUICK_WINDOW_H_
