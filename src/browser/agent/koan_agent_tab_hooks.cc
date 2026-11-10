// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_tab_hooks.h"

namespace koan::agent {

namespace {

TabHooksDelegate* g_delegate = nullptr;
AgentTabOpener* g_opener = nullptr;

}  // namespace

TabHooksDelegate::~TabHooksDelegate() = default;
AgentTabOpener::~AgentTabOpener() = default;

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

}  // namespace koan::agent
