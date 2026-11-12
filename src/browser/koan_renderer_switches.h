// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_RENDERER_SWITCHES_H_
#define KOAN_BROWSER_KOAN_RENDERER_SWITCHES_H_

namespace base {
class CommandLine;
}

namespace content {
class RenderProcessHost;
}

namespace koan {

// Adds the Koan settings that blink reads to a renderer's command line:
// the Glance trigger method (koan_features.h), from the profile of
// `process`. Called by the hook in
// ChromeContentBrowserClient::AppendExtraCommandLineSwitches().
void AppendRendererSwitches(base::CommandLine* command_line,
                            content::RenderProcessHost& process);

// Called when a setting AppendRendererSwitches() passes on changes (Glance
// on or off, its trigger). A page sees it only in a renderer process started
// after the change, and content keeps a spare process started in advance,
// with the old switches, for the next page: this drops it, so the next page
// gets a process with the new ones, as the settings page says.
void DropSpareRenderers();

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_RENDERER_SWITCHES_H_
