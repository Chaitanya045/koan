// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_crash_restore.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_prefs.h"
#include "koan/common/koan_branding.h"

namespace koan {

namespace {

void SetResumedCrashes(base::WeakPtr<Profile> profile, int count) {
  if (profile) {
    profile->GetOriginalProfile()->GetPrefs()->SetInteger(
        prefs::kResumedCrashes, count);
  }
}

// Runs after the launch, like the bubble's Restore button. Holds the crashed
// lock until the restore has started, so that the exit type service waits for
// it to finish before it takes the crash as acknowledged.
void RestoreNow(base::WeakPtr<BrowserWindowInterface> browser,
                std::unique_ptr<ExitTypeService::CrashedLock> lock) {
  if (!browser || browser_shutdown::HasShutdownStarted() ||
      browser->IsDeleteScheduled()) {
    return;
  }
  SessionRestore::RestoreSessionAfterCrash(browser.get());
}

}  // namespace

bool RestoreAfterCrash(BrowserWindowInterface* browser,
                       bool post_crash_launch) {
  Profile* profile = browser->GetProfile();
  if (profile->IsOffTheRecord()) {
    return false;
  }
  PrefService* prefs = profile->GetPrefs();
  const int resumed = prefs->GetInteger(prefs::kResumedCrashes);
  if (!post_crash_launch) {
    // The last session ended cleanly: a later crash restores again.
    if (resumed != 0) {
      prefs->SetInteger(prefs::kResumedCrashes, 0);
    }
    return false;
  }
  if (!kRestoreAfterCrashWithoutAsking) {
    return false;
  }
  // Restored already in this process (a Dock reopen later), or restoring now:
  // not a second time.
  if (profile->restored_last_session() ||
      SessionRestore::IsRestoring(profile)) {
    return true;
  }
  // The last launch was a silent restore and it crashed too.
  if (resumed >= 1) {
    return false;
  }
  ExitTypeService* exit_type_service =
      ExitTypeService::GetInstanceForProfile(profile);
  if (!exit_type_service) {
    return false;
  }
  prefs->SetInteger(prefs::kResumedCrashes, resumed + 1);
  // Once the restore is done and the crash acknowledged, the count starts
  // again: this crash was dealt with.
  exit_type_service->AddCrashAckCallback(
      base::BindOnce(&SetResumedCrashes, profile->GetWeakPtr(), 0));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&RestoreNow, browser->GetWeakPtr(),
                                exit_type_service->CreateCrashedLock()));
  return true;
}

}  // namespace koan
