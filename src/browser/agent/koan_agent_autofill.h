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

#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/autofill/core/browser/foundations/autofill_client.h"
#include "components/autofill/core/browser/foundations/autofill_manager.h"
#include "components/autofill/core/browser/foundations/scoped_autofill_managers_observation.h"
#include "components/autofill/core/browser/suggestions/suggestion.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/unique_ids.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "koan/browser/agent/koan_agent_autofill_logic.h"
#include "koan/browser/agent/koan_agent_facade.h"

namespace autofill {
class AutofillSuggestionDelegate;
}

namespace blink {
class WebInputEvent;
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
// of them hold a secret Koan filled, and the values it filled (for the
// scrubber). And it knows which frames have a secret field with any value in
// it, whoever filled it (the password manager on load, the user typing it,
// the page): it reads the password fields and the fields marked
// autocomplete="cc-*" in each frame's Chrome-only isolated world (neither
// the page nor the agent sees that world, and what it reads stays in the
// browser), and the other fields autofill classifies as secret from
// autofill's own copy of the form. A frame with such a value is locked (no
// script until it navigates or the field is cleared), and each value found
// goes to the scrubber for the tab's life in the registry, as the ones Koan
// filled do (a request already sent may still be read after the page moved
// on).
//
// The read is cached per frame and made again only once something could
// have changed a field: input in the tab (the user's or the agent's), a
// fill, a form submitted, a navigation, Hand back. The facade asks before a
// script read (SecretsCurrent(), RefreshSecrets()); input also reads again
// kSecretReadDelay after it, so that the scrubber knows a value before the
// form that holds it is sent. Its cost: one querySelectorAll('input') per
// frame, at most once per script command after input and once per
// kSecretReadDelay while the user types.
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
  // Whether those frames' secret fields were read since anything changed;
  // reads every frame that was not, then runs `done` (posted).
  bool SecretsCurrent(content::RenderFrameHost* root);
  void RefreshSecrets(base::OnceClosure done);
  // Something may have changed a field (`soon`: read again shortly).
  void InvalidateSecrets(bool soon);
  FacadeBackend::CardFields CardFields();
  // Every secret value Koan filled in the tab.
  const std::vector<std::string>& secret_values() const { return values_; }
  // Every secret value found in its fields that Koan did not fill.
  const std::vector<std::string>& found_values() const { return found_; }

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
  void OnAfterFormSubmitted(autofill::AutofillManager& manager,
                            const autofill::FormData& form) override;
  void OnAfterFormsSeen(
      autofill::AutofillManager& manager,
      base::span<const autofill::FormGlobalId> updated_forms,
      base::span<const autofill::FormGlobalId> removed_forms) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void RenderFrameDeleted(content::RenderFrameHost* frame) override;
  void DidGetUserInteraction(const blink::WebInputEvent& event) override;

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
  // Asks the field's renderer for its suggestions, as a click on it does
  // (for the request in flight, if it is still for `field`).
  void TriggerSuggestions(autofill::FieldGlobalId field);
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

  // A frame's secret fields as last read (any value, whoever filled it).
  struct FrameSecrets {
    // The generation (secrets_generation_) the read started at; 0: never.
    uint64_t read_at = 0;
    // A read in flight, by its token (0: none).
    uint64_t reading = 0;
    // A secret field held a value.
    bool filled = false;
    // The frame did not answer in time: counted as filled.
    bool unknown = false;
  };
  void ReadSecretFields();
  void OnSecretFieldsRead(content::FrameTreeNodeId frame,
                          uint64_t token,
                          uint64_t generation,
                          base::Value values);
  void OnSecretReadTimeout();
  void MaybeFinishSecretRead();
  // A value found in a secret field: the scrubber's for the tab's life.
  void NoteFound(const std::string& value);

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
  // The other secret values found in its fields, for its life.
  std::vector<std::string> found_;
  std::map<content::FrameTreeNodeId, FrameSecrets> frame_secrets_;
  uint64_t secrets_generation_ = 1;
  uint64_t next_read_token_ = 1;
  std::vector<base::OnceClosure> secrets_waiting_;
  base::OneShotTimer read_soon_;
  base::OneShotTimer read_timeout_;
  autofill::FieldGlobalId focused_;
  // The field Koan last asked suggestions for, and when that ask goes to its
  // renderer (Run(): Chrome drops a second ask too soon after).
  autofill::FieldGlobalId last_ask_field_;
  base::TimeTicks last_ask_;
  // ShowCrossDomainConfirmationPopup() was asked during the pick.
  bool cross_domain_refused_ = false;
  bool accepting_ = false;
  base::WeakPtrFactory<AgentAutofill> weak_factory_{this};
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_AUTOFILL_H_
