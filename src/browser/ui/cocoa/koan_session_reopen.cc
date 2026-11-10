// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/cocoa/koan_session_reopen.h"

#include "base/command_line.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/startup/startup_tab.h"

namespace koan {

bool ReopenLastSession(Profile* profile) {
  if (!profile || profile->IsOffTheRecord() ||
      browser_shutdown::HasShutdownStarted()) {
    return false;
  }
  // A second Dock click while the first one's restore reads the session: its
  // window is on the way, so no empty window next to it.
  if (SessionRestore::IsRestoring(profile)) {
    return true;
  }
  // The same check that SessionService::RestoreIfNecessary() makes, asked
  // first: that function moves the current session to the last one before it
  // looks at the pref, and a move with no restore after it would lose the
  // window. (On a first-run process the pref reads as the default, not
  // "continue".)
  if (!StartupBrowserCreator::GetSessionStartupPref(
           *base::CommandLine::ForCurrentProcess(), profile)
           .ShouldRestoreLastSession()) {
    return false;
  }
  SessionService* service =
      SessionServiceFactory::GetForProfileForSessionRestore(profile);
  // With no browser window open, the last window's close is still pending in
  // the session (SessionService::WindowClosing), so the last session is that
  // window. RestoreIfNecessary() is what -[AppController
  // applicationShouldHandleReopen:] already calls for a hidden login item
  // (AttemptSessionRestore); it creates the window. It declines while a
  // tabbed browser is open or a restore runs.
  return service && service->RestoreIfNecessary(StartupTabs(),
                                                /*restore_apps=*/false);
}

}  // namespace koan
