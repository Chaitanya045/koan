// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_HOOKS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_HOOKS_H_

#include "base/memory/weak_ptr.h"
#include "components/autofill/core/browser/foundations/autofill_client.h"

namespace autofill {
class AutofillSuggestionDelegate;
}

namespace content {
class WebContents;
}

// The calls Chrome's autofill and password manager make into agent control
// for `koan autofill` (.agent/notes/agent-control.md section 7.0, "Autofill
// for agents"; docs/REBASE.md), kept in a target that depends on
// //components/autofill only so that chrome/browser/ui/autofill and
// chrome/browser/password_manager can use it. The browser side
// (AgentForwarder) installs the delegate; with none installed every call
// returns false and Chrome behaves as upstream.
namespace koan::agent {

class AutofillHooksDelegate {
 public:
  virtual ~AutofillHooksDelegate();

  // See TakeAgentSuggestions().
  virtual bool TakeSuggestions(
      content::WebContents* contents,
      const autofill::AutofillClient::PopupOpenArgs& open_args,
      base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate) = 0;
  // See RefusesAgentCrossDomainFill().
  virtual bool RefusesCrossDomainFill(content::WebContents* contents) = 0;
};

// Installs (or, with null, removes) the delegate. UI thread.
void SetAutofillHooksDelegate(AutofillHooksDelegate* delegate);

// ChromeAutofillClient::ShowAutofillSuggestions(): whether agent control took
// the suggestions instead of the popup, because an agent asked for this
// field's (Koan.autofill). Covers autofill's and the password manager's,
// which both show through it. The agent then sees the rows' labels, and its
// pick goes to `delegate` as the user's click on the row would.
bool TakeAgentSuggestions(
    content::WebContents* contents,
    const autofill::AutofillClient::PopupOpenArgs& open_args,
    base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate);

// ChromePasswordManagerClient::ShowCrossDomainConfirmationPopup(): whether
// to show nothing, and fill nothing, because an agent's pick is running in
// `contents`. Chrome asks the user to confirm a login saved for a related
// site (a grouped affiliation match); an agent may only fill a login saved
// for this site, so its pick is refused (Koan.offSite) instead.
bool RefusesAgentCrossDomainFill(content::WebContents* contents);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_HOOKS_H_
