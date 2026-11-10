// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_REACH_CONTENT_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_REACH_CONTENT_H_

#include <vector>

#include "koan/browser/agent/koan_agent_reach.h"

namespace content {
class RenderFrameHost;
}

// The browser side of koan_agent_reach.h: what a RenderFrameHost and each of
// its ancestors (crossing into embedders) are, from public //content and
// //chrome accessors only.
namespace koan::agent {

// `frame` first, then its parent or outer document or embedder, up to the
// outermost main frame. Empty for a null frame.
std::vector<FrameFacts> FactsForFrameAndAncestors(
    content::RenderFrameHost* frame);

// MayReachFrame() of those facts.
bool MayReachRenderFrameHost(content::RenderFrameHost* frame);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_REACH_CONTENT_H_
