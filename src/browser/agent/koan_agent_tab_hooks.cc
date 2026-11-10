// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_tab_hooks.h"

#include <utility>

#include "net/http/http_response_headers.h"

namespace koan::agent {

namespace {

TabHooksDelegate* g_delegate = nullptr;
AgentTabOpener* g_opener = nullptr;
PageSurfaceDelegate* g_surfaces = nullptr;

// The delegate if `contents` is held by an agent, else null.
PageSurfaceDelegate* SurfacesFor(content::WebContents* contents) {
  return g_surfaces && contents && g_surfaces->IsHeldByAgent(contents)
             ? g_surfaces
             : nullptr;
}

}  // namespace

TabHooksDelegate::~TabHooksDelegate() = default;
AgentTabOpener::~AgentTabOpener() = default;
PageSurfaceDelegate::~PageSurfaceDelegate() = default;

HttpAuthRequest::HttpAuthRequest() = default;
HttpAuthRequest::HttpAuthRequest(HttpAuthRequest&&) = default;
HttpAuthRequest& HttpAuthRequest::operator=(HttpAuthRequest&&) = default;
HttpAuthRequest::~HttpAuthRequest() = default;

void SetAgentTabOpener(AgentTabOpener* opener) {
  g_opener = opener;
}

AgentTabOpener* GetAgentTabOpener() {
  return g_opener;
}

void SetTabHooksDelegate(TabHooksDelegate* delegate) {
  g_delegate = delegate;
}

bool OpensInBackgroundForAgent(content::WebContents* source,
                               WindowOpenDisposition* disposition) {
  if (!g_delegate || !source || !g_delegate->IsAgentTab(source)) {
    return false;
  }
  switch (*disposition) {
    case WindowOpenDisposition::kNewForegroundTab:
    case WindowOpenDisposition::kNewPopup:
    case WindowOpenDisposition::kNewWindow:
    case WindowOpenDisposition::kNewPictureInPicture:
    case WindowOpenDisposition::kSingletonTab:
    case WindowOpenDisposition::kSwitchToTab:
    case WindowOpenDisposition::kNewSplitView:
    // Never an Incognito window: agent control has no Incognito (section 13).
    case WindowOpenDisposition::kOffTheRecord:
      *disposition = WindowOpenDisposition::kNewBackgroundTab;
      return true;
    case WindowOpenDisposition::kUnknown:
    case WindowOpenDisposition::kCurrentTab:
    case WindowOpenDisposition::kNewBackgroundTab:
    case WindowOpenDisposition::kSaveToDisk:
    case WindowOpenDisposition::kIgnoreAction:
      return false;
  }
  return false;
}

void OnOpenedFromAgentTab(content::WebContents* source,
                          content::WebContents* opened) {
  if (g_delegate && source && opened && source != opened &&
      g_delegate->IsAgentTab(source)) {
    g_delegate->OnOpenedFromAgentTab(source, opened);
  }
}

void SetPageSurfaceDelegate(PageSurfaceDelegate* delegate) {
  g_surfaces = delegate;
}

content::JavaScriptDialogManager* JavaScriptDialogManagerForAgent(
    content::WebContents* contents) {
  PageSurfaceDelegate* surfaces = SurfacesFor(contents);
  return surfaces ? surfaces->DialogManager() : nullptr;
}

bool HoldExternalProtocolForAgent(
    content::WebContents* contents,
    const GURL& url,
    ui::PageTransition page_transition,
    bool has_user_gesture,
    bool is_in_fenced_frame_tree,
    const std::optional<url::Origin>& initiating_origin,
    content::RenderFrameHost* initiator_document) {
  PageSurfaceDelegate* surfaces = SurfacesFor(contents);
  if (!surfaces) {
    return false;
  }
  surfaces->HoldExternalProtocol(contents, url, page_transition,
                                 has_user_gesture, is_in_fenced_frame_tree,
                                 initiating_origin, initiator_document);
  return true;
}

bool ShouldHoldHttpAuthForAgent(content::WebContents* contents,
                                content::GuestPageHolder* guest,
                                bool do_not_prompt_for_login) {
  return !guest && !do_not_prompt_for_login && SurfacesFor(contents);
}

std::unique_ptr<content::LoginDelegate> HoldHttpAuthForAgent(
    content::WebContents* web_contents,
    content::BrowserContext* browser_context,
    const net::AuthChallengeInfo& auth_info,
    const content::GlobalRequestID& request_id,
    bool is_request_for_primary_main_frame_navigation,
    bool is_request_for_navigation,
    const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    content::LoginDelegate::LoginAuthRequiredCallback auth_required_callback) {
  // Only after ShouldHoldHttpAuthForAgent() said yes.
  PageSurfaceDelegate* surfaces = SurfacesFor(web_contents);
  CHECK(surfaces);
  HttpAuthRequest request;
  request.web_contents = web_contents;
  request.browser_context = browser_context;
  request.auth_info = auth_info;
  request.request_id = request_id;
  request.is_request_for_primary_main_frame_navigation =
      is_request_for_primary_main_frame_navigation;
  request.is_request_for_navigation = is_request_for_navigation;
  request.url = url;
  request.response_headers = std::move(response_headers);
  request.auth_required_callback = std::move(auth_required_callback);
  return surfaces->HoldHttpAuth(std::move(request));
}

bool HoldPrintForAgent(content::WebContents* contents,
                       base::OnceCallback<void(bool show)> show) {
  PageSurfaceDelegate* surfaces = SurfacesFor(contents);
  return surfaces && surfaces->HoldPrint(contents, std::move(show));
}

bool HoldActivationForAgent(content::WebContents* contents) {
  PageSurfaceDelegate* surfaces = SurfacesFor(contents);
  if (!surfaces) {
    return false;
  }
  surfaces->HoldActivation(contents);
  return true;
}

}  // namespace koan::agent
