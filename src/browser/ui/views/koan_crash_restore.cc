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
#include "base/logging.h"
#include "base/time/time.h"
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

// A silent restore that was this long ago worked; a crash after that is a new
// crash, restored silently again. A crash sooner than that (a crash loop)
// shows the bubble on the next launch. Compared by the clock at the next
// launch, not with a timer: a timer in a hidden app can be coalesced for a
// long time (App Nap) and then never run before a kill.
constexpr base::TimeDelta kStableAfter = base::Seconds(60);

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
  int resumed = prefs->GetInteger(prefs::kResumedCrashes);
  const base::Time last = prefs->GetTime(prefs::kLastResumedTime);
  const base::Time now = base::Time::Now();
  VLOG(1) << "Koan: crash restore: resumed_crashes=" << resumed
          << " last_resumed=" << last << " crashed=" << post_crash_launch
          << " restored=" << profile->restored_last_session();
  if (!post_crash_launch) {
    // The last session ended cleanly: a later crash restores again.
    if (resumed != 0) {
      prefs->SetInteger(prefs::kResumedCrashes, 0);
      // On disk now: a kill -9 a few seconds into this launch must not bring
      // the old count back.
      prefs->CommitPendingWrite();
    }
    VLOG(1) << "Koan: crash restore: action=none (clean launch)";
    return false;
  }
  if (!kRestoreAfterCrashWithoutAsking) {
    return false;
  }
  // Restored already in this process (a Dock reopen later), or restoring now:
  // not a second time.
  if (profile->restored_last_session() ||
      SessionRestore::IsRestoring(profile)) {
    VLOG(1) << "Koan: crash restore: action=none (already restored)";
    return true;
  }
  if (resumed >= 1 && now - last >= kStableAfter) {
    // The last silent restore is long enough ago: this is a new crash.
    resumed = 0;
  }
  // The last launch was a silent restore and it crashed too.
  if (resumed >= 1) {
    VLOG(1) << "Koan: crash restore: action=bubble";
    return false;
  }
  ExitTypeService* exit_type_service =
      ExitTypeService::GetInstanceForProfile(profile);
  if (!exit_type_service) {
    return false;
  }
  prefs->SetInteger(prefs::kResumedCrashes, resumed + 1);
  prefs->SetTime(prefs::kLastResumedTime, now);
  // Written now, not with the next lazy write: a crash within seconds of this
  // launch (kill -9 included) must still find the count on disk.
  prefs->CommitPendingWrite();
  VLOG(1) << "Koan: crash restore: action=silent";
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&RestoreNow, browser->GetWeakPtr(),
                                exit_type_service->CreateCrashedLock()));
  return true;
}

}  // namespace koan
