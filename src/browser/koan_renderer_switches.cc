// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_renderer_switches.h"

#include <string>

#include "base/command_line.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "ipc/ipc_channel_proxy.h"
#include "koan/browser/koan_prefs.h"
#include "koan/common/koan_glance_trigger.h"
#include "koan/common/koan_renderer_settings.mojom.h"
#include "mojo/public/cpp/bindings/associated_remote.h"

namespace koan {

namespace {

// The prefs of `process`'s profile, if it has any. Incognito's read through
// to the regular profile's.
const PrefService* PrefsOf(content::RenderProcessHost& process) {
  content::BrowserContext* context = process.GetBrowserContext();
  return user_prefs::UserPrefs::IsInitialized(context)
             ? user_prefs::UserPrefs::Get(context)
             : nullptr;
}

std::string GlanceActivationMethod(const PrefService& prefs) {
  return prefs.GetBoolean(prefs::kGlanceEnabled)
             ? prefs.GetString(prefs::kGlanceActivationMethod)
             : switches::kGlanceOff;
}

}  // namespace

void AppendRendererSwitches(base::CommandLine* command_line,
                            content::RenderProcessHost& process) {
  const PrefService* prefs = PrefsOf(process);
  if (!prefs) {
    return;
  }
  command_line->AppendSwitchASCII(switches::kGlanceActivationMethod,
                                  GlanceActivationMethod(*prefs));
}

void UpdateRendererSettings() {
  // Every process, the spare one too, each with its own profile's settings:
  // a change in one profile resends the others' unchanged, which is harmless.
  for (content::RenderProcessHost::iterator it(
           content::RenderProcessHost::AllHostsIterator());
       !it.IsAtEnd(); it.Advance()) {
    content::RenderProcessHost* process = it.GetCurrentValue();
    // No channel before Init(), which builds the command line from the
    // current prefs. Messages to a process still launching wait for it, and
    // come after its command line, so the last value wins.
    IPC::ChannelProxy* channel = process->GetChannel();
    const PrefService* prefs = PrefsOf(*process);
    if (!channel || !prefs) {
      continue;
    }
    mojo::AssociatedRemote<mojom::RendererSettings> settings;
    channel->GetRemoteAssociatedInterface(&settings);
    settings->SetGlanceActivationMethod(GlanceActivationMethod(*prefs));
  }
}

}  // namespace koan
