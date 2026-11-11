// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_agent_access.h"

#include <vector>

#include "base/strings/strcat.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/containers/koan_container_partition.h"

namespace koan::containers {

namespace {

const char16_t* NameOf(AgentCapability capability) {
  switch (capability) {
    case AgentCapability::kLogins:
      return u"your logins";
    case AgentCapability::kPasswords:
      return u"saved passwords";
    case AgentCapability::kAutofill:
      return u"addresses and cards";
    case AgentCapability::kExtensions:
      return u"your extensions";
    case AgentCapability::kHistory:
      return u"history";
  }
  return u"";
}

// "a", "a and b", "a, b and c".
std::u16string List(const std::vector<AgentCapability>& capabilities) {
  std::u16string text;
  for (size_t i = 0; i < capabilities.size(); ++i) {
    if (i > 0) {
      text += i + 1 == capabilities.size() ? u" and " : u", ";
    }
    text += NameOf(capabilities[i]);
  }
  return text;
}

}  // namespace

void DenyAllAgentAccess(PrefService* prefs) {
  for (AgentCapability capability : kAgentCapabilities) {
    prefs->SetBoolean(AgentAccessPref(capability), false);
  }
}

bool AgentMay(const PrefService* prefs, AgentCapability capability) {
  if (!prefs) {
    return false;
  }
  const PrefService::Preference* pref =
      prefs->FindPreference(AgentAccessPref(capability));
  return pref && pref->GetValue()->is_bool() && pref->GetValue()->GetBool();
}

bool AgentMay(content::BrowserContext* context, AgentCapability capability) {
  return context && user_prefs::UserPrefs::IsInitialized(context) &&
         AgentMay(user_prefs::UserPrefs::Get(context), capability);
}

bool LimitsTakeOverToSite(const PrefService* prefs) {
  const PrefService::Preference* pref =
      prefs ? prefs->FindPreference(kAgentLimitTakeOverToSite) : nullptr;
  return !pref || !pref->GetValue()->is_bool() || pref->GetValue()->GetBool();
}

bool LimitsTakeOverToSite(content::BrowserContext* context) {
  return !context || !user_prefs::UserPrefs::IsInitialized(context) ||
         LimitsTakeOverToSite(user_prefs::UserPrefs::Get(context));
}

bool BlocksAgent(const content::WebContents* contents,
                 AgentCapability capability) {
  // GetBrowserContext() isn't const; nothing here changes the tab.
  return IsAgentContents(contents) &&
         !AgentMay(
             const_cast<content::WebContents*>(contents)->GetBrowserContext(),
             capability);
}

bool BlocksAgentFrame(content::RenderFrameHost* frame,
                      AgentCapability capability) {
  return IsAgentTabFrame(frame) &&
         !AgentMay(frame->GetBrowserContext(), capability);
}

bool BlocksAgentProcess(content::RenderProcessHost* process,
                        AgentCapability capability) {
  return IsAgentProcess(process) &&
         !AgentMay(process->GetBrowserContext(), capability);
}

std::u16string DescribeAgentAccess(const PrefService* prefs, bool strict) {
  if (strict) {
    return u"It can open and work in its own background tabs. Strict mode is "
           u"on: each task runs in a hidden profile of its own, with none of "
           u"your logins, saved passwords, addresses, extensions or history.";
  }
  std::vector<AgentCapability> uses;
  std::vector<AgentCapability> not_uses;
  for (AgentCapability capability : kAgentCapabilities) {
    (AgentMay(prefs, capability) ? uses : not_uses).push_back(capability);
  }
  std::u16string text =
      AgentMay(prefs, AgentCapability::kLogins)
          ? u"It can open and work in its own background tabs."
          : u"It can open and work in its own background tabs, in a "
            u"container of their own: a throwaway one that is cleared when "
            u"its task ends, or one you saved for it.";
  if (!uses.empty()) {
    text += base::StrCat({u" They use ", List(uses), u"."});
  }
  if (!not_uses.empty()) {
    text += base::StrCat({u" They don't get ", List(not_uses), u"."});
  }
  return base::StrCat(
      {text,
       u" Change this in Settings > Agent Control. Your cookies and sign-in "
       u"tokens are never sent to the program."});
}

}  // namespace koan::containers
