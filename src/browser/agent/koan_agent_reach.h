// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_REACH_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_REACH_H_

#include "base/containers/span.h"
#include "url/gurl.h"

// Which frames an agent's DevTools client may attach to
// (.agent/notes/agent-control.md section 4.4, threat T7). A strict Koan-only
// allowlist that depends on no upstream internals: the browser side describes
// a frame and its ancestors as FrameFacts (koan_agent_reach_content.h fills
// them from a RenderFrameHost), and this decides. The equivalent predicates in
// //chrome's debugger API are file-local, and copying them would follow their
// changes silently; this table only ever gets stricter by itself.
namespace koan::agent {

// What the browser knows about one frame.
struct FrameFacts {
  // The last committed URL, and the URL of the frame's security principal
  // (which is updated before a navigation commits, so a frame on its way to a
  // privileged page is caught early).
  GURL committed_url;
  GURL site_url;
  // The frame hosts a WebUI (chrome://, Koan's settings, the agent control
  // page), or its WebContents is privileged (//chrome's PrivilegedWebContents).
  bool web_ui = false;
  bool privileged_contents = false;
  // The frame runs in a PDF process, or is a MimeHandler/guest view's frame.
  bool pdf_or_guest = false;
  // The tab shows a security interstitial (the agent must not click Proceed).
  bool interstitial = false;
  // The frame's profile is off the record (Incognito, Guest) or a system
  // profile: never reachable in v1 (design section 13).
  bool off_the_record_or_system = false;
};

enum class Reach {
  kAllowed,
  kWrongScheme,
  kWebUi,
  kPrivileged,
  kPdfOrGuest,
  kInterstitial,
  kOffTheRecord,
};

// The URLs a frame the agent reaches may have: http and https, the about:blank
// and about:srcdoc documents (which take their parent's origin), the network
// error page (so a failed navigation does not detach the tab), an empty URL
// (nothing committed yet), and blob: and data: documents in frames the agent
// did not navigate (blob: only with an http or https inner origin; data: is an
// opaque origin with no privilege). Every other scheme (chrome:, file:,
// chrome-extension:, devtools:, view-source:, filesystem:, ...) is not.
//
// This is wider than IsAllowedNavigationUrl(), which is what the agent itself
// may navigate to: a page may contain a data: iframe without the agent losing
// the tab.
bool IsReachableFrameUrl(const GURL& url);

// Whether the agent may reach `frame_and_ancestors[0]`, given every frame from
// it up to the outermost main frame, crossing into embedders (a guest's
// embedder, a portal's or fenced frame's outer document): each of them must
// pass. An empty span is refused.
Reach MayReachFrame(base::span<const FrameFacts> frame_and_ancestors);

// DevToolsAgentHostClient::MayAttachToURL(): `is_webui` refuses, otherwise
// IsReachableFrameUrl().
bool MayAttachToUrl(const GURL& url, bool is_webui);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_REACH_H_
