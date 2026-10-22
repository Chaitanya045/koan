// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_tab_hooks.h"

#include <memory>

#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_pinned.h"

namespace koan {

namespace {

constexpr char kActivationKey[] = "koan_tab_activation";

struct Activation : public base::SupportsUserData::Data {
  explicit Activation(base::TimeTicks activated) : time(activated) {}
  base::TimeTicks time;
};

// Incognito's PrefService reads Koan's prefs through to the regular profile
// (Koan never writes them there; see koan_prefs.h).
bool GetBoolean(content::BrowserContext* context, const char* pref) {
  return user_prefs::UserPrefs::Get(context)->GetBoolean(pref);
}

}  // namespace

void OnTabActivated(content::WebContents* contents) {
  contents->SetUserData(kActivationKey,
                        std::make_unique<Activation>(base::TimeTicks::Now()));
}

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
    const auto* activation = contents
                                 ? static_cast<const Activation*>(
                                       contents->GetUserData(kActivationKey))
                                 : nullptr;
    if (activation && (!best || activation->time > best_time)) {
      best = i;
      best_time = activation->time;
    }
  }
  return best;
}

bool ShouldCloseTabOnBack(content::WebContents* contents) {
  // Never a pinned tab: upstream only knows Essentials as pinned.
  return contents &&
         GetBoolean(contents->GetBrowserContext(),
                    prefs::kCloseOnBackWithNoHistory) &&
         !IsPinnedForUser(contents);
}

}  // namespace koan
