// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_renderer_switches.h"

#include "base/command_line.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/spare_render_process_host_manager.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_switches.h"

namespace koan {

void AppendRendererSwitches(base::CommandLine* command_line,
                            content::RenderProcessHost& process) {
  // Incognito's prefs read through to the regular profile's.
  const PrefService* pref_service =
      user_prefs::UserPrefs::Get(process.GetBrowserContext());
  if (!pref_service) {
    return;
  }
  command_line->AppendSwitchASCII(
      switches::kGlanceActivationMethod,
      pref_service->GetBoolean(prefs::kGlanceEnabled)
          ? pref_service->GetString(prefs::kGlanceActivationMethod)
          : switches::kGlanceOff);
}

void DropSpareRenderers() {
  // The manager's one public way to drop its spares (gracefully: unbound
  // processes, no page in them); it warms a new one when a navigation needs
  // it. Despite the name, it is the call this needs.
  content::SpareRenderProcessHostManager::Get().CleanupSparesForTesting();
}

}  // namespace koan
