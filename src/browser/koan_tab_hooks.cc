// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_tab_hooks.h"

#include "base/time/time.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_prefs.h"

namespace koan {

namespace {

// Incognito's PrefService reads Koan's prefs through to the regular profile
// (Koan never writes them there; see koan_prefs.h).
bool GetBoolean(content::BrowserContext* context, const char* pref) {
  return user_prefs::UserPrefs::Get(context)->GetBoolean(pref);
}

}  // namespace

std::optional<int> PickRecentlyUsedTab(
    content::BrowserContext* context,
    int count,
    base::FunctionRef<content::WebContents*(int)> contents_at) {
  if (!context || !GetBoolean(context, prefs::kSelectRecentlyUsedOnClose)) {
    return std::nullopt;
  }
  std::optional<int> best;
  base::TimeTicks best_time;
  for (int i = 0; i < count; ++i) {
    content::WebContents* contents = contents_at(i);
    if (contents && (!best || contents->GetLastActiveTimeTicks() > best_time)) {
      best = i;
      best_time = contents->GetLastActiveTimeTicks();
    }
  }
  return best;
}

bool ShouldCloseTabOnBack(content::WebContents* contents) {
  return GetBoolean(contents->GetBrowserContext(),
                    prefs::kCloseOnBackWithNoHistory);
}

}  // namespace koan
