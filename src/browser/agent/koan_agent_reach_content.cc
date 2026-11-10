// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_reach_content.h"

#include "chrome/browser/profiles/profile.h"
#include "components/security_interstitials/content/security_interstitial_tab_helper.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/security_principal.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"

namespace koan::agent {

namespace {

FrameFacts FactsFor(content::RenderFrameHost* frame) {
  FrameFacts facts;
  facts.committed_url = frame->GetLastCommittedURL();
  // Updated before a navigation commits: a frame on its way to a privileged
  // page is caught before it gets there.
  facts.site_url =
      frame->GetSiteInstance()->GetSecurityPrincipal().GetDeprecatedSiteURL();
  facts.web_ui = frame->GetWebUI() != nullptr;
  content::RenderProcessHost* process = frame->GetProcess();
  facts.pdf_or_guest = process->IsPdf() || process->IsForGuestsOnly();
  if (content::WebContents* contents =
          content::WebContents::FromRenderFrameHost(frame)) {
    facts.privileged_contents = contents->IsPrivileged();
    facts.pdf_or_guest =
        facts.pdf_or_guest || contents->GetOuterWebContents() != nullptr;
    if (auto* interstitials = security_interstitials::
            SecurityInterstitialTabHelper::FromWebContents(contents)) {
      facts.interstitial = interstitials->IsDisplayingInterstitial();
    }
  }
  Profile* profile = Profile::FromBrowserContext(frame->GetBrowserContext());
  facts.off_the_record_or_system = !profile || profile->IsOffTheRecord() ||
                                   profile->IsSystemProfile() ||
                                   profile->IsGuestSession();
  return facts;
}

}  // namespace

std::vector<FrameFacts> FactsForFrameAndAncestors(
    content::RenderFrameHost* frame) {
  std::vector<FrameFacts> facts;
  for (; frame; frame = frame->GetParentOrOuterDocumentOrEmbedder()) {
    facts.push_back(FactsFor(frame));
  }
  return facts;
}

bool MayReachRenderFrameHost(content::RenderFrameHost* frame) {
  const std::vector<FrameFacts> facts = FactsForFrameAndAncestors(frame);
  return MayReachFrame(facts) == Reach::kAllowed;
}

}  // namespace koan::agent
