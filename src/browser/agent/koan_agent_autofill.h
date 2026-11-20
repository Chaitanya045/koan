// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/autofill/core/browser/foundations/autofill_client.h"
#include "components/autofill/core/browser/foundations/autofill_manager.h"
#include "components/autofill/core/browser/foundations/scoped_autofill_managers_observation.h"
#include "components/autofill/core/browser/suggestions/suggestion.h"
#include "components/autofill/core/common/unique_ids.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "koan/browser/agent/koan_agent_autofill_logic.h"
#include "koan/browser/agent/koan_agent_facade.h"

namespace autofill {
class AutofillSuggestionDelegate;
}

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

// `koan autofill` in the browser (.agent/notes/agent-control.md section 7.0,
// "Autofill for agents"): an agent uses the user's addresses, cards and saved
// logins the way the user does, through Chrome's own suggestions and fill,
// and never reads the secrets it fills (section 4.5).
//
// Listing asks the field's renderer for suggestions as a click on it would
// (AutofillDriver::RendererShouldTriggerSuggestions()); autofill or the
// password manager then shows them through ChromeAutofillClient, whose hook
// (TakeAgentSuggestions(), koan_agent_autofill_hooks.h) hands them here
// instead of the popup. A pick goes to the suggestion's delegate as the
// popup's row click does (AutofillSuggestionDelegate::DidAcceptSuggestion()):
// Chrome's own fill, with its own checks, the card's unmasking and the
// password manager's matching. What Chrome would ask the user first (Touch ID
// or the device password, a CVC) becomes a prompt in the agent indicator
// (PageSurface::kAutofillVerify), and the fill runs once the user opens it.
//
// It also knows, for the facade, the secret fields of the tab's frames, which
// of them hold a secret Koan filled (a frame with one is locked: no script
// until it navigates or the field is cleared), and the values it filled (for
// the scrubber).
//
// One per tab in the agent registry, owned by the forwarder's TabWatcher, UI
// thread.
namespace koan::agent {

class AgentAutofill final : public autofill::AutofillManager::Observer,
                            public content::WebContentsObserver {
 public:
  // `on_secret` runs with each secret value Koan filled (UTF-8), for the
  // client's scrubber.
  AgentAutofill(content::WebContents* contents,
                base::RepeatingCallback<void(const std::string&)> on_secret);
  AgentAutofill(const AgentAutofill&) = delete;
  AgentAutofill& operator=(const AgentAutofill&) = delete;
  ~AgentAutofill() override;

  // Koan.autofill on the field `request.node` in the frames of `root`'s
  // process. `agent` is the client's label, for the indicator.
  void Run(content::RenderFrameHost* root,
           const FacadeBackend::AutofillRequest& request,
           const std::string& agent,
           FacadeBackend::AutofillCallback done);

  // The hooks (koan_agent_autofill_hooks.h).
  bool TakeSuggestions(
      const autofill::AutofillClient::PopupOpenArgs& open_args,
      base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate);
  bool RefusesCrossDomainFill();

  // For the facade: the secret fields of the frames of `root`'s process.
  FacadeBackend::SecretFacts SecretsIn(content::RenderFrameHost* root);
  FacadeBackend::CardFields CardFields();
  // Every secret value Koan filled in the tab.
  const std::vector<std::string>& secret_values() const { return values_; }

  // autofill::AutofillManager::Observer:
  void OnAfterFocusOnFormField(autofill::AutofillManager& manager,
                               autofill::FormGlobalId form,
                               autofill::FieldGlobalId field) override;
  void OnAfterFocusOnNonFormField(autofill::AutofillManager& manager) override;
  void OnAfterTextFieldValueChanged(autofill::AutofillManager& manager,
                                    autofill::FormGlobalId form,
                                    autofill::FieldGlobalId field) override;
  void OnAfterJavaScriptChangedAutofilledValue(
      autofill::AutofillManager& manager,
      autofill::FormGlobalId form,
      autofill::FieldGlobalId field) override;
  void OnFillOrPreviewForm(
      autofill::AutofillManager& manager,
      autofill::FormGlobalId form_id,
      autofill::FieldGlobalId trigger_field_id,
      autofill::mojom::ActionPersistence action_persistence,
      const base::flat_set<autofill::FieldGlobalId>& filled_field_ids,
      const base::flat_map<
          autofill::FieldGlobalId,
          autofill::DenseSet<autofill::FieldFillingSkipReason>>& skip_reasons,
      const autofill::FillingPayload& filling_payload) override;
  void OnFillOrPreviewField(
      autofill::AutofillManager& manager,
      autofill::FormGlobalId form_id,
      autofill::FieldGlobalId field_id,
      autofill::mojom::ActionPersistence action_persistence,
      const std::u16string& value,
      std::optional<autofill::FieldType> field_type_used) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void RenderFrameDeleted(content::RenderFrameHost* frame) override;

 private:
  struct Request;
  // A secret field Koan filled, still holding what it filled.
  struct Filled {
    content::FrameTreeNodeId frame;
    // A card number, CVC or IBAN (painted over in screenshots).
    bool masked = false;
  };

  // What Chrome offered, once the hook took it (posted: never inside
  // ShowAutofillSuggestions()).
  void OnSuggestions(
      std::vector<autofill::Suggestion> suggestions,
      autofill::FormGlobalId form,
      autofill::FieldGlobalId field,
      base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate);
  // No suggestions came: the error, by the switch the field needs.
  void OnNoSuggestions();
  // Accepts the row as its click would; then waits for the fill.
  void Accept();
  // The fill of a password: its values, read in the frame's Chrome-only
  // world, are the secrets.
  void ReadPasswords(int tries);
  void OnPasswordsRead(int tries, base::Value values);
  // The fill was seen (or not, by the time it should have been).
  void OnFilled();
  void OnFillTimeout();
  void Finish(FacadeBackend::AutofillResult result);
  // The user opened the verify prompt: the tab is in front, the fill runs and
  // Chrome asks them.
  void RunVerifiedFill();

  // Remembers a secret Koan filled in `field` (in the frame `frame`).
  void NoteFilled(const autofill::FieldGlobalId& field,
                  content::RenderFrameHost* frame,
                  bool masked);
  void NoteSecret(const std::u16string& value);
  // Whether the frame of `field` still holds a value Koan filled: its
  // fields are read in its Chrome-only world.
  void CheckCleared(const autofill::FieldGlobalId& field);
  void OnClearedCheck(content::FrameTreeNodeId frame, base::Value values);
  void ForgetFrame(content::FrameTreeNodeId frame);

  // The field's form in autofill's cache, and its frame.
  struct FoundField {
    raw_ptr<autofill::AutofillManager> manager = nullptr;
    raw_ptr<content::RenderFrameHost> frame = nullptr;
    autofill::FieldGlobalId field;
  };
  std::optional<FoundField> FindField(content::RenderFrameHost* root, int node);
  content::RenderFrameHost* FrameOf(const autofill::FieldGlobalId& field);

  const base::RepeatingCallback<void(const std::string&)> on_secret_;
  autofill::ScopedAutofillManagersObservation autofill_observation_{this};
  std::unique_ptr<Request> request_;
  // A pick that waits for the user to open its prompt.
  std::unique_ptr<Request> verify_;
  std::map<autofill::FieldGlobalId, Filled> filled_;
  // The secret values Koan filled in the tab, for its life (in memory only).
  std::vector<std::string> values_;
  autofill::FieldGlobalId focused_;
  // ShowCrossDomainConfirmationPopup() was asked during the pick.
  bool cross_domain_refused_ = false;
  bool accepting_ = false;
  base::WeakPtrFactory<AgentAutofill> weak_factory_{this};
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_H_
