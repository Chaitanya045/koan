// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_tab_unload.h"

#include <algorithm>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/performance_manager/policies/cannot_discard_reason.h"
#include "chrome/browser/performance_manager/policies/discard_eligibility_policy.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "components/performance_manager/public/performance_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

namespace koan {

bool IsTabUnloaded(content::WebContents* contents) {
  return contents->WasDiscarded() || contents->GetController().NeedsReload();
}

bool IsTabInUse(content::WebContents* contents) {
  using performance_manager::PerformanceManager;
  using performance_manager::policies::CannotDiscardReason;
  using performance_manager::policies::DiscardEligibilityPolicy;
  // Without Memory Saver's data, assume the page is in use (keep it).
  if (!PerformanceManager::IsAvailable()) {
    return true;
  }
  base::WeakPtr<performance_manager::PageNode> page =
      PerformanceManager::GetPrimaryPageNodeForWebContents(contents);
  auto* policy =
      DiscardEligibilityPolicy::GetFromGraph(PerformanceManager::GetGraph());
  if (!page || !policy) {
    return true;
  }
  std::vector<CannotDiscardReason> reasons;
  policy->CanDiscard(page.get(),
                     DiscardEligibilityPolicy::DiscardReason::URGENT,
                     /*ignore_recent_visibility=*/true, &reasons);
  return std::ranges::any_of(reasons, [](CannotDiscardReason reason) {
    switch (reason) {
      case CannotDiscardReason::kAlreadyDiscarded:
      case CannotDiscardReason::kDiscardAttempted:
      case CannotDiscardReason::kRecentlyAudible:
      case CannotDiscardReason::kRecentlyVisible:
      case CannotDiscardReason::kNotWebOrInternal:
      case CannotDiscardReason::kInvalidURL:
      case CannotDiscardReason::kPinnedTab:
      case CannotDiscardReason::kNotificationsEnabled:
      case CannotDiscardReason::kBackgroundActivity:
      case CannotDiscardReason::kGlicShared:
        return false;
      default:
        return true;
    }
  });
}

void UnloadTab(content::WebContents* contents) {
  if (auto* unit =
          resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
              contents)) {
    unit->DiscardTab(::mojom::LifecycleUnitDiscardReason::EXTERNAL);
  }
}

}  // namespace koan
