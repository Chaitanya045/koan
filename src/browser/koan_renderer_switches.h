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

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_RENDERER_SWITCHES_H_
