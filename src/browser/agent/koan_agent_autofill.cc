// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_autofill.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/autofill/content/browser/content_autofill_client.h"
#include "components/autofill/content/browser/content_autofill_driver.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/data_manager/payments/payments_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_model/payments/credit_card.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/payments/credit_card_access_manager.h"
#include "components/autofill/core/browser/payments/payments_autofill_client.h"
#include "components/autofill/core/browser/ui/autofill_suggestion_delegate.h"
#include "components/autofill/core/common/aliases.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/agent/koan_agent_page_surfaces.h"
#include "koan/browser/agent/koan_agent_secret_fields.h"
#include "koan/browser/agent/koan_agent_secrets.h"
#include "koan/browser/containers/koan_agent_access.h"
#include "net/base/schemeful_site.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "components/password_manager/core/common/password_manager_pref_names.h"
#endif

namespace koan::agent {

namespace {

using autofill::Suggestion;
using autofill::SuggestionType;

// How long the renderer and autofill have to offer suggestions, and to fill
// once a row is accepted.
constexpr base::TimeDelta kSuggestionWait = base::Seconds(3);
constexpr base::TimeDelta kFillWait = base::Seconds(3);
// A login's fill is not reported to the browser: its frame is read this
// often, this many times, until its password field holds a value.
constexpr base::TimeDelta kPasswordReadInterval = base::Milliseconds(150);
constexpr int kPasswordReadTries = 10;

// In the frame's Chrome-only isolated world (neither the page nor the agent
// sees it, and its answer stays in the browser): the password fields'
// values, and every field's value.
constexpr char16_t kReadPasswordsScript[] =
    uR"((() => Array.from(document.querySelectorAll('input[type=password]'),
    (e) => e.value).filter((v) => v).slice(0, 20))())";
constexpr char16_t kReadValuesScript[] =
    uR"((() => Array.from(document.querySelectorAll('input, textarea'),
    (e) => e.value).filter((v) => v).slice(0, 500))())";

// The rows an agent may pick, by what they fill. Everything else (manage,
// passkeys, generation, footers, separators) is not offered.
std::optional<AutofillKind> KindOf(SuggestionType type) {
  switch (type) {
    case SuggestionType::kAddressEntry:
    case SuggestionType::kAddressEntryOnTyping:
    case SuggestionType::kAddressFieldByFieldFilling:
      return AutofillKind::kAddress;
    case SuggestionType::kCreditCardEntry:
    case SuggestionType::kVirtualCreditCardEntry:
      return AutofillKind::kCard;
    case SuggestionType::kIbanEntry:
      return AutofillKind::kIban;
    case SuggestionType::kPasswordEntry:
    case SuggestionType::kAccountStoragePasswordEntry:
      return AutofillKind::kPassword;
    case SuggestionType::kAutocompleteEntry:
    case SuggestionType::kDatalistEntry:
      return AutofillKind::kPrevious;
    default:
      return std::nullopt;
  }
}

SuggestionRow RowFor(const Suggestion& suggestion, AutofillKind kind) {
  SuggestionRow row;
  row.kind = kind;
  row.main_text = suggestion.main_text.value;
  for (const Suggestion::Text& text : suggestion.minor_texts) {
    row.minor_texts.push_back(text.value);
  }
  for (const std::vector<Suggestion::Text>& line : suggestion.labels) {
    std::vector<std::u16string>& texts = row.labels.emplace_back();
    for (const Suggestion::Text& text : line) {
      texts.push_back(text.value);
    }
  }
  row.additional_label = suggestion.additional_label;
  return row;
}

autofill::ContentAutofillClient* ClientOf(content::WebContents* contents) {
  return autofill::ContentAutofillClient::FromWebContents(contents);
}

// The saved card a card row fills, if Chrome has it.
const autofill::CreditCard* CardOf(content::WebContents* contents,
                                   const Suggestion& suggestion) {
  autofill::ContentAutofillClient* client = ClientOf(contents);
  const auto* guid = std::get_if<Suggestion::Guid>(&suggestion.payload);
  if (!client || !guid) {
    return nullptr;
  }
  return client->GetPersonalDataManager()
      .payments_data_manager()
      .GetCreditCardByGUID(guid->value());
}

// A card number as the page may show it: digits only, and in groups of four
// with spaces or dashes.
std::vector<std::u16string> CardNumberForms(const std::u16string& number) {
  std::u16string digits;
  for (char16_t c : number) {
    if (base::IsAsciiDigit(c)) {
      digits.push_back(c);
    }
  }
  std::vector<std::u16string> forms = {number, digits};
  for (char16_t separator : {u' ', u'-'}) {
    std::u16string grouped;
    for (size_t i = 0; i < digits.size(); ++i) {
      if (i && i % 4 == 0) {
        grouped.push_back(separator);
      }
      grouped.push_back(digits[i]);
    }
    forms.push_back(grouped);
  }
  return forms;
}

// Whether Chrome asks the user before it fills `suggestion` (a card that
// must be unmasked, mandatory reauth; a login with "Use Touch ID before
// filling" on).
bool NeedsUserToVerify(content::WebContents* contents,
                       const Suggestion& suggestion,
                       AutofillKind kind) {
  if (kind == AutofillKind::kCard) {
    const autofill::CreditCard* card = CardOf(contents, suggestion);
    autofill::ContentAutofillClient* client = ClientOf(contents);
    autofill::payments::PaymentsAutofillClient* payments =
        client ? client->GetPaymentsAutofillClient() : nullptr;
    return !card ||
           card->record_type() !=
               autofill::CreditCard::RecordType::kLocalCard ||
           !payments || payments->IsMandatoryReauthEnabled();
  }
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  if (kind == AutofillKind::kPassword) {
    const PrefService* prefs =
        user_prefs::UserPrefs::Get(contents->GetBrowserContext());
    return prefs &&
           prefs->FindPreference(password_manager::prefs::
                                     kBiometricAuthenticationBeforeFilling) &&
           prefs->GetBoolean(
               password_manager::prefs::kBiometricAuthenticationBeforeFilling);
  }
#endif
  return false;
}

FacadeBackend::AutofillResult Error(std::string code, std::string message) {
  FacadeBackend::AutofillResult result;
  result.error_code = std::move(code);
  result.error_message = std::move(message);
  return result;
}

}  // namespace

// A Koan.autofill in progress: the field, and once a row is picked, the row
// and the delegate it goes to. A pick that waits for the user (verify_) keeps
// it until they open the prompt.
struct AgentAutofill::Request {
  FacadeBackend::AutofillRequest request;
  std::string agent;
  FacadeBackend::AutofillCallback done;
  autofill::FieldGlobalId field;
  content::FrameTreeNodeId frame;
  // The field is in a login form (a password field in its form).
  bool login_form = false;
  bool taken = false;
  base::OneShotTimer timer;
  // Once picked.
  bool picked = false;
  bool filled = false;
  std::optional<Suggestion> suggestion;
  size_t row = 0;
  AutofillKind kind = AutofillKind::kAddress;
  std::string label;
  autofill::FormGlobalId form;
  base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate;
  // The prompt in the indicator, while it waits for the user.
  uint64_t prompt = 0;
  // The user opened that prompt: Chrome asks them, and may take as long as
  // they do.
  bool verified = false;
};

AgentAutofill::AgentAutofill(
    content::WebContents* contents,
    base::RepeatingCallback<void(const std::string&)> on_secret)
    : content::WebContentsObserver(contents), on_secret_(std::move(on_secret)) {
  // Agent containers' tabs may have no autofill client.
  if (autofill::ContentAutofillClient* client = ClientOf(contents)) {
    autofill_observation_.Observe(
        client, autofill::ScopedAutofillManagersObservation::
                    InitializationPolicy::kObservePreexistingManagers);
  }
}

AgentAutofill::~AgentAutofill() {
  if (verify_ && verify_->prompt) {
    AgentPageSurfaces::Get().ForgetAutofillVerify(verify_->prompt);
  }
}

content::RenderFrameHost* AgentAutofill::FrameOf(
    const autofill::FieldGlobalId& field) {
  content::RenderFrameHost* found = nullptr;
  web_contents()->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
    if (found) {
      return;
    }
    if (auto* driver =
            autofill::ContentAutofillDriver::GetForRenderFrameHost(frame);
        driver && driver->GetFrameToken() == field.frame_token) {
      found = frame;
    }
  });
  return found;
}

std::optional<AgentAutofill::FoundField> AgentAutofill::FindField(
    content::RenderFrameHost* root,
    int node) {
  // A backend DOM node id is the field's renderer id in its frame, and is
  // unique in its renderer process; the form it is in may be cached by the
  // manager of an ancestor frame (autofill's form flattening).
  std::vector<content::RenderFrameHost*> frames;
  std::vector<autofill::AutofillManager*> managers;
  web_contents()->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
    auto* driver =
        autofill::ContentAutofillDriver::GetForRenderFrameHost(frame);
    if (!driver || !frame->IsRenderFrameLive()) {
      return;
    }
    managers.push_back(&driver->GetAutofillManager());
    if (frame->GetProcess() == root->GetProcess()) {
      frames.push_back(frame);
    }
  });
  for (content::RenderFrameHost* frame : frames) {
    const autofill::FieldGlobalId id{
        autofill::ContentAutofillDriver::GetForRenderFrameHost(frame)
            ->GetFrameToken(),
        autofill::FieldRendererId(static_cast<uint64_t>(node))};
    for (autofill::AutofillManager* manager : managers) {
      if (std::as_const(*manager).FindCachedFormById(id)) {
        return FoundField{manager, frame, id};
      }
    }
  }
  return std::nullopt;
}

void AgentAutofill::Run(content::RenderFrameHost* root,
                        const FacadeBackend::AutofillRequest& request,
                        const std::string& agent,
                        FacadeBackend::AutofillCallback done) {
  if (request_) {
    std::move(done).Run(Error("Koan.busy",
                              "another koan autofill is running in this tab; "
                              "wait for it"));
    return;
  }
  if (verify_) {
    std::move(done).Run(
        Error("Koan.userNeeded",
              "an earlier pick waits for the user to verify it in Koan (Koan."
              "userNeeded autofill-verify); wait for Koan.userNeededDone"));
    return;
  }
  std::optional<FoundField> found =
      root ? FindField(root, request.node) : std::nullopt;
  if (!found) {
    std::move(done).Run(Error(
        "Koan.notAField",
        "Chrome's autofill does not know that element as a form field (take "
        "a new snapshot and use a text field's ref)"));
    return;
  }
  auto pending = std::make_unique<Request>();
  pending->request = request;
  pending->agent = agent;
  pending->done = std::move(done);
  pending->field = found->field;
  pending->frame = found->frame->GetFrameTreeNodeId();
  if (const autofill::FormStructure* form =
          std::as_const(*found->manager).FindCachedFormById(found->field)) {
    for (const std::unique_ptr<autofill::AutofillField>& field :
         form->fields()) {
      pending->login_form =
          pending->login_form || field->form_control_type() ==
                                     autofill::FormControlType::kInputPassword;
    }
  }
  request_ = std::move(pending);
  request_->timer.Start(FROM_HERE, kSuggestionWait,
                        base::BindOnce(&AgentAutofill::OnNoSuggestions,
                                       weak_factory_.GetWeakPtr()));
  // As a click on the field asks for them.
  autofill::AutofillDriver* driver =
      autofill::ContentAutofillDriver::GetForRenderFrameHost(found->frame);
  driver->RendererShouldTriggerSuggestions(
      found->field,
      autofill::AutofillSuggestionTriggerSource::kFormControlElementClicked);
}

bool AgentAutofill::TakeSuggestions(
    const autofill::AutofillClient::PopupOpenArgs& open_args,
    base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate) {
  if (!request_ || request_->picked || open_args.field_id != request_->field) {
    return false;
  }
  if (request_->taken) {
    // An update of the same suggestions: still no popup.
    return true;
  }
  request_->taken = true;
  request_->timer.Stop();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AgentAutofill::OnSuggestions, weak_factory_.GetWeakPtr(),
                     open_args.suggestions, open_args.form_id,
                     open_args.field_id, std::move(delegate)));
  return true;
}

bool AgentAutofill::RefusesCrossDomainFill() {
  if (!accepting_) {
    return false;
  }
  cross_domain_refused_ = true;
  return true;
}

void AgentAutofill::OnSuggestions(
    std::vector<Suggestion> suggestions,
    autofill::FormGlobalId form,
    autofill::FieldGlobalId field,
    base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate) {
  if (!request_) {
    return;
  }
  std::vector<size_t> offered;
  std::vector<AutofillKind> kinds;
  std::vector<std::string> labels;
  for (size_t i = 0; i < suggestions.size(); ++i) {
    const Suggestion& suggestion = suggestions[i];
    const std::optional<AutofillKind> kind = KindOf(suggestion.type);
    if (!kind || !suggestion.IsAcceptable()) {
      continue;
    }
    // What the label must never show: the card's number.
    std::vector<std::u16string> secrets;
    if (const autofill::CreditCard* card =
            *kind == AutofillKind::kCard ? CardOf(web_contents(), suggestion)
                                         : nullptr) {
      secrets = CardNumberForms(card->number());
    }
    offered.push_back(i);
    kinds.push_back(*kind);
    labels.push_back(LabelFor(RowFor(suggestion, *kind), secrets));
  }
  if (offered.empty()) {
    OnNoSuggestions();
    return;
  }
  if (request_->request.list) {
    base::ListValue rows;
    for (size_t j = 0; j < offered.size(); ++j) {
      rows.Append(base::DictValue()
                      .Set("index", static_cast<int>(j + 1))
                      .Set("label", labels[j])
                      .Set("kind", AutofillKindName(kinds[j])));
    }
    FacadeBackend::AutofillResult result;
    result.result.Set("suggestions", std::move(rows));
    result.summary = base::StrCat({"list ", AutofillKindName(kinds.front()),
                                   " ", base::NumberToString(offered.size())});
    Finish(std::move(result));
    return;
  }
  const PickMatch match = MatchPick(labels, request_->request.pick);
  if (!match.index) {
    Finish(Error(match.error_code, match.error_message));
    return;
  }
  const size_t j = *match.index;
  request_->picked = true;
  request_->suggestion = suggestions[offered[j]];
  request_->row = offered[j];
  request_->kind = kinds[j];
  request_->label = labels[j];
  request_->form = form;
  request_->field = field;
  request_->delegate = std::move(delegate);
  const std::string summary =
      base::StrCat({AutofillKindName(request_->kind), " ", request_->label});

  // The switch the row needs (the hooks already keep Chrome from offering
  // it with the switch off; this says so if it changed meanwhile).
  const bool password = request_->kind == AutofillKind::kPassword;
  if (koan::containers::BlocksAgent(
          web_contents(), password
                              ? koan::containers::AgentCapability::kPasswords
                              : koan::containers::AgentCapability::kAutofill)) {
    auto result = Error("Koan.autofillOff",
                        password ? "the user does not let agents use saved "
                                   "passwords (Settings > Agent Control)"
                                 : "the user does not let agents use addresses "
                                   "and cards (Settings > Agent Control)");
    result.summary = summary;
    Finish(std::move(result));
    return;
  }
  // A login goes only to a form that sends it to the site it is in, which
  // is the site Chrome matched the login for (its own PSL and affiliation
  // rules).
  if (password) {
    content::RenderFrameHost* frame = FrameOf(request_->field);
    const autofill::FormStructure* structure = nullptr;
    web_contents()->ForEachRenderFrameHost([&](content::RenderFrameHost* rfh) {
      if (auto* driver =
              autofill::ContentAutofillDriver::GetForRenderFrameHost(rfh);
          driver && !structure) {
        structure = std::as_const(driver->GetAutofillManager())
                        .FindCachedFormById(request_->field);
      }
    });
    const GURL action = structure ? structure->target_url() : GURL();
    const GURL page = frame ? frame->GetLastCommittedURL() : GURL();
    if (!frame || !page.SchemeIsHTTPOrHTTPS() ||
        (action.SchemeIsHTTPOrHTTPS() &&
         net::SchemefulSite(action) != net::SchemefulSite(page))) {
      auto result = Error(
          "Koan.offSite",
          "this form sends what is typed in it to another site than the one "
          "the login is saved for; Koan does not fill it");
      result.summary = summary;
      Finish(std::move(result));
      return;
    }
  }

  if (!request_->verified &&
      NeedsUserToVerify(web_contents(), *request_->suggestion,
                        request_->kind)) {
    std::unique_ptr<Request> waiting = std::move(request_);
    const std::string detail =
        base::StrCat({password ? "login:" : "card:", waiting->agent});
    const uint64_t prompt = AgentPageSurfaces::Get().HoldAutofillVerify(
        web_contents(), detail,
        base::BindOnce(&AgentAutofill::RunVerifiedFill,
                       weak_factory_.GetWeakPtr()),
        base::BindOnce(
            [](base::WeakPtr<AgentAutofill> self) {
              if (self) {
                self->verify_.reset();
              }
            },
            weak_factory_.GetWeakPtr()));
    FacadeBackend::AutofillCallback done = std::move(waiting->done);
    FacadeBackend::AutofillResult result;
    result.summary = summary;
    if (!prompt) {
      result = Error("Koan.userNeeded",
                     "Chrome asks the user to verify this pick, and the "
                     "agent indicator could not ask them; try later");
      result.summary = summary;
      std::move(done).Run(std::move(result));
      return;
    }
    waiting->prompt = prompt;
    waiting->done = base::DoNothing();
    verify_ = std::move(waiting);
    result.result.Set("filled", false);
    result.result.Set("userNeeded", "autofill-verify");
    result.result.Set("kind", AutofillKindName(verify_->kind));
    result.result.Set("label", verify_->label);
    std::move(done).Run(std::move(result));
    return;
  }
  Accept();
}

void AgentAutofill::RunVerifiedFill() {
  if (!verify_) {
    return;
  }
  // The prompt left the queue when the user opened it.
  verify_->prompt = 0;
  if (request_) {
    // Another autofill started meanwhile: this one is dropped.
    verify_.reset();
    return;
  }
  request_ = std::move(verify_);
  // The suggestions are asked for again, so that the row goes to the
  // delegate as it is now (the page may have moved focus meanwhile), and
  // the same label picked.
  request_->verified = true;
  request_->picked = false;
  request_->taken = false;
  request_->request.list = false;
  request_->request.pick = AutofillPick{.label = request_->label};
  content::RenderFrameHost* frame = FrameOf(request_->field);
  autofill::AutofillDriver* driver =
      frame ? autofill::ContentAutofillDriver::GetForRenderFrameHost(frame)
            : nullptr;
  if (!driver) {
    request_.reset();
    return;
  }
  request_->timer.Start(FROM_HERE, kSuggestionWait,
                        base::BindOnce(&AgentAutofill::OnNoSuggestions,
                                       weak_factory_.GetWeakPtr()));
  driver->RendererShouldTriggerSuggestions(
      request_->field,
      autofill::AutofillSuggestionTriggerSource::kFormControlElementClicked);
}

void AgentAutofill::Accept() {
  base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate =
      request_->delegate;
  if (!delegate) {
    Finish(Error("Koan.pageChanged",
                 "the page changed before the pick was made; list again"));
    return;
  }
  autofill::AutofillSuggestionDelegate::SuggestionMetadata metadata;
  metadata.multi_index = {request_->row};
  const autofill::Suggestion suggestion = *request_->suggestion;
  const autofill::FormGlobalId form = request_->form;
  const autofill::FieldGlobalId field = request_->field;
  const bool password = request_->kind == AutofillKind::kPassword;
  // A login fills its frame's password field: the frame is locked from the
  // pick on, before its value can be read.
  if (password) {
    if (content::RenderFrameHost* frame = FrameOf(field)) {
      if (auto* driver =
              autofill::ContentAutofillDriver::GetForRenderFrameHost(frame)) {
        std::as_const(driver->GetAutofillManager())
            .ForEachCachedForm([&](const autofill::FormStructure& structure) {
              for (const std::unique_ptr<autofill::AutofillField>& candidate :
                   structure.fields()) {
                if (candidate->host_frame() == field.frame_token &&
                    candidate->form_control_type() ==
                        autofill::FormControlType::kInputPassword) {
                  NoteFilled(candidate->global_id(), frame, /*masked=*/false);
                }
              }
            });
      }
    }
  }
  cross_domain_refused_ = false;
  accepting_ = true;
  base::WeakPtr<AgentAutofill> self = weak_factory_.GetWeakPtr();
  // As the popup's row click does.
  delegate->DidAcceptSuggestion(suggestion, metadata, form, field);
  if (!self) {
    return;
  }
  accepting_ = false;
  if (!request_) {
    return;
  }
  if (cross_domain_refused_) {
    Finish(Error("Koan.offSite",
                 "this login is saved for a related site, and Chrome asks the "
                 "user to confirm it is for this one; an agent may not, so "
                 "nothing was filled (the user can pick it)"));
    CheckCleared(field);
    return;
  }
  if (password) {
    ReadPasswords(0);
    return;
  }
  if (request_->filled) {
    OnFilled();
    return;
  }
  // A card Chrome unmasks first, which Koan did not foresee: Chrome asks
  // the user in the tab, and the indicator says so.
  if (request_->kind == AutofillKind::kCard) {
    content::RenderFrameHost* frame = FrameOf(field);
    auto* driver =
        frame ? autofill::ContentAutofillDriver::GetForRenderFrameHost(frame)
              : nullptr;
    autofill::CreditCardAccessManager* access =
        driver ? driver->GetAutofillManager().GetCreditCardAccessManager()
               : nullptr;
    if (access && !access->ShouldClearPreviewedForm()) {
      AgentPageSurfaces::Get().HoldAutofillVerify(
          web_contents(), base::StrCat({"card:", request_->agent}),
          base::DoNothing(), base::DoNothing());
      FacadeBackend::AutofillResult result;
      result.summary = base::StrCat({"card ", request_->label});
      result.result.Set("filled", false);
      result.result.Set("userNeeded", "autofill-verify");
      result.result.Set("kind", "card");
      result.result.Set("label", request_->label);
      Finish(std::move(result));
      return;
    }
  }
  request_->timer.Start(FROM_HERE, kFillWait,
                        base::BindOnce(&AgentAutofill::OnFillTimeout,
                                       weak_factory_.GetWeakPtr()));
}

void AgentAutofill::ReadPasswords(int tries) {
  if (!request_) {
    return;
  }
  content::RenderFrameHost* frame = FrameOf(request_->field);
  if (!frame || !frame->IsRenderFrameLive()) {
    Finish(Error("Koan.pageChanged", "the page went away during the fill"));
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      kReadPasswordsScript,
      base::BindOnce(&AgentAutofill::OnPasswordsRead,
                     weak_factory_.GetWeakPtr(), tries),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AgentAutofill::OnPasswordsRead(int tries, base::Value values) {
  if (!request_) {
    return;
  }
  bool any = false;
  if (values.is_list()) {
    for (const base::Value& value : values.GetList()) {
      if (const std::string* text = value.GetIfString();
          text && !text->empty()) {
        any = true;
        NoteSecret(base::UTF8ToUTF16(*text));
      }
    }
  }
  if (any) {
    OnFilled();
    return;
  }
  // After Touch ID the fill waits for the user: read for a minute then.
  const int most =
      request_->verified ? kPasswordReadTries * 40 : kPasswordReadTries;
  if (tries + 1 < most) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&AgentAutofill::ReadPasswords,
                       weak_factory_.GetWeakPtr(), tries + 1),
        kPasswordReadInterval);
    return;
  }
  const autofill::FieldGlobalId field = request_->field;
  OnFillTimeout();
  // Nothing was filled: the frame is not locked for nothing.
  CheckCleared(field);
}

void AgentAutofill::OnFilled() {
  FacadeBackend::AutofillResult result;
  result.summary =
      base::StrCat({AutofillKindName(request_->kind), " ", request_->label});
  result.result.Set("filled", true);
  result.result.Set("kind", AutofillKindName(request_->kind));
  result.result.Set("label", request_->label);
  Finish(std::move(result));
}

void AgentAutofill::OnFillTimeout() {
  if (!request_) {
    return;
  }
  auto result =
      Error("Koan.notFilled",
            "Chrome did not fill the form (it may wait for the user, or the "
            "page refused it); take a snapshot to see the field");
  result.summary =
      base::StrCat({AutofillKindName(request_->kind), " ", request_->label});
  Finish(std::move(result));
}

void AgentAutofill::OnNoSuggestions() {
  if (!request_) {
    return;
  }
  const bool login = request_->login_form;
  if (login &&
      koan::containers::BlocksAgent(
          web_contents(), koan::containers::AgentCapability::kPasswords)) {
    Finish(Error("Koan.autofillOff",
                 "the user does not let agents use saved passwords (Settings "
                 "> Agent Control > What agents can use)"));
    return;
  }
  if (!login &&
      koan::containers::BlocksAgent(
          web_contents(), koan::containers::AgentCapability::kAutofill)) {
    Finish(Error("Koan.autofillOff",
                 "the user does not let agents use addresses and cards "
                 "(Settings > Agent Control > What agents can use)"));
    return;
  }
  Finish(Error("Koan.noSuggestions",
               "Chrome has nothing saved to suggest for this field; type into "
               "it instead"));
}

void AgentAutofill::Finish(FacadeBackend::AutofillResult result) {
  if (!request_) {
    return;
  }
  FacadeBackend::AutofillCallback done = std::move(request_->done);
  request_.reset();
  std::move(done).Run(std::move(result));
}

void AgentAutofill::NoteFilled(const autofill::FieldGlobalId& field,
                               content::RenderFrameHost* frame,
                               bool masked) {
  if (!frame) {
    return;
  }
  Filled& filled = filled_[field];
  filled.frame = frame->GetFrameTreeNodeId();
  filled.masked = filled.masked || masked;
}

void AgentAutofill::NoteSecret(const std::u16string& value) {
  const std::string text = base::UTF16ToUTF8(value);
  if (text.size() < kMinSecretLength ||
      std::ranges::find(values_, text) != values_.end()) {
    return;
  }
  values_.push_back(text);
  on_secret_.Run(text);
}

void AgentAutofill::CheckCleared(const autofill::FieldGlobalId& field) {
  content::RenderFrameHost* frame = FrameOf(field);
  if (!frame || !frame->IsRenderFrameLive()) {
    return;
  }
  const content::FrameTreeNodeId id = frame->GetFrameTreeNodeId();
  if (std::ranges::none_of(
          filled_, [&](const auto& item) { return item.second.frame == id; })) {
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      kReadValuesScript,
      base::BindOnce(&AgentAutofill::OnClearedCheck, weak_factory_.GetWeakPtr(),
                     id),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AgentAutofill::OnClearedCheck(content::FrameTreeNodeId frame,
                                   base::Value values) {
  if (!values.is_list()) {
    return;
  }
  for (const base::Value& value : values.GetList()) {
    const std::string* text = value.GetIfString();
    if (!text) {
      continue;
    }
    std::string digits;
    for (char c : *text) {
      if (base::IsAsciiDigit(c)) {
        digits.push_back(c);
      }
    }
    for (const std::string& secret : values_) {
      if (*text == secret ||
          (digits.size() >= kMinSecretLength && digits == secret)) {
        return;  // still there
      }
    }
  }
  ForgetFrame(frame);
}

void AgentAutofill::ForgetFrame(content::FrameTreeNodeId frame) {
  std::erase_if(filled_,
                [&](const auto& item) { return item.second.frame == frame; });
}

FacadeBackend::SecretFacts AgentAutofill::SecretsIn(
    content::RenderFrameHost* root) {
  FacadeBackend::SecretFacts facts;
  if (!root) {
    return facts;
  }
  // The frames script in `root`'s process can reach, by their autofill
  // frame token.
  std::set<autofill::LocalFrameToken> tokens;
  std::set<content::FrameTreeNodeId> frames;
  std::vector<autofill::AutofillManager*> managers;
  web_contents()->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
    auto* driver =
        autofill::ContentAutofillDriver::GetForRenderFrameHost(frame);
    if (driver) {
      managers.push_back(&driver->GetAutofillManager());
    }
    if (frame->GetProcess() != root->GetProcess()) {
      return;
    }
    frames.insert(frame->GetFrameTreeNodeId());
    if (driver) {
      tokens.insert(driver->GetFrameToken());
    }
  });
  for (const auto& [field, filled] : filled_) {
    facts.locked = facts.locked || frames.contains(filled.frame);
    facts.card_filled = facts.card_filled || filled.masked;
  }
  facts.focused_locked = focused_ && filled_.contains(focused_);
  for (autofill::AutofillManager* manager : managers) {
    std::as_const(*manager).ForEachCachedForm(
        [&](const autofill::FormStructure& form) {
          for (const std::unique_ptr<autofill::AutofillField>& field :
               form.fields()) {
            if (tokens.contains(field->host_frame()) && IsSecretField(*field)) {
              facts.fields.insert(
                  static_cast<int>(field->renderer_id().value()));
            }
          }
        });
  }
  return facts;
}

FacadeBackend::CardFields AgentAutofill::CardFields() {
  FacadeBackend::CardFields fields;
  content::RenderFrameHost* main = web_contents()->GetPrimaryMainFrame();
  std::set<std::string> frames;
  for (const auto& [field, filled] : filled_) {
    if (!filled.masked) {
      continue;
    }
    content::RenderFrameHost* frame =
        web_contents()->UnsafeFindFrameByFrameTreeNodeId(filled.frame);
    if (!frame) {
      continue;
    }
    if (frame->GetProcess() == main->GetProcess()) {
      fields.nodes.push_back(static_cast<int>(field.renderer_id.value()));
      continue;
    }
    // The frame in another process whose <iframe> is in the main frame's.
    while (frame->GetParent() &&
           frame->GetParent()->GetProcess() != main->GetProcess()) {
      frame = frame->GetParent();
    }
    frames.insert(frame->GetDevToolsFrameToken().ToString());
  }
  fields.frames.assign(frames.begin(), frames.end());
  return fields;
}

void AgentAutofill::OnAfterFocusOnFormField(autofill::AutofillManager& manager,
                                            autofill::FormGlobalId form,
                                            autofill::FieldGlobalId field) {
  focused_ = field;
}

void AgentAutofill::OnAfterFocusOnNonFormField(
    autofill::AutofillManager& manager) {
  focused_ = autofill::FieldGlobalId();
}

void AgentAutofill::OnAfterTextFieldValueChanged(
    autofill::AutofillManager& manager,
    autofill::FormGlobalId form,
    autofill::FieldGlobalId field) {
  CheckCleared(field);
}

void AgentAutofill::OnAfterJavaScriptChangedAutofilledValue(
    autofill::AutofillManager& manager,
    autofill::FormGlobalId form,
    autofill::FieldGlobalId field) {
  CheckCleared(field);
}

void AgentAutofill::OnFillOrPreviewForm(
    autofill::AutofillManager& manager,
    autofill::FormGlobalId form_id,
    autofill::FieldGlobalId trigger_field_id,
    autofill::mojom::ActionPersistence action_persistence,
    const base::flat_set<autofill::FieldGlobalId>& filled_field_ids,
    const base::flat_map<autofill::FieldGlobalId,
                         autofill::DenseSet<autofill::FieldFillingSkipReason>>&
        skip_reasons,
    const autofill::FillingPayload& filling_payload) {
  if (action_persistence != autofill::mojom::ActionPersistence::kFill) {
    return;
  }
  // Whoever filled it (the agent, or the user in a tab the agent holds),
  // what autofill put in a secret field is kept from the agent.
  const autofill::FormStructure* form =
      std::as_const(manager).FindCachedFormById(form_id);
  for (const autofill::FieldGlobalId& id : filled_field_ids) {
    const autofill::AutofillField* field =
        form ? form->GetFieldById(id) : nullptr;
    if (field && IsSecretField(*field)) {
      NoteFilled(id, FrameOf(id), IsMaskedField(*field));
    }
  }
  if (const auto* card =
          std::get_if<const autofill::CreditCard*>(&filling_payload);
      card && *card) {
    for (const std::u16string& number : CardNumberForms((*card)->number())) {
      NoteSecret(number);
    }
  }
  if (request_ && request_->picked) {
    request_->filled = true;
    if (!accepting_) {
      request_->timer.Stop();
      OnFilled();
    }
  }
}

void AgentAutofill::OnFillOrPreviewField(
    autofill::AutofillManager& manager,
    autofill::FormGlobalId form_id,
    autofill::FieldGlobalId field_id,
    autofill::mojom::ActionPersistence action_persistence,
    const std::u16string& value,
    std::optional<autofill::FieldType> field_type_used) {
  if (action_persistence != autofill::mojom::ActionPersistence::kFill) {
    return;
  }
  const autofill::FormStructure* form =
      std::as_const(manager).FindCachedFormById(form_id);
  const autofill::AutofillField* field =
      form ? form->GetFieldById(field_id) : nullptr;
  if (field && IsSecretField(*field)) {
    NoteFilled(field_id, FrameOf(field_id), IsMaskedField(*field));
    NoteSecret(value);
  }
  if (request_ && request_->picked) {
    request_->filled = true;
    if (!accepting_) {
      request_->timer.Stop();
      OnFilled();
    }
  }
}

void AgentAutofill::DidFinishNavigation(content::NavigationHandle* handle) {
  if (!handle->HasCommitted() || handle->IsSameDocument()) {
    return;
  }
  const content::FrameTreeNodeId frame = handle->GetFrameTreeNodeId();
  ForgetFrame(frame);
  if (verify_ && verify_->frame == frame) {
    if (verify_->prompt) {
      AgentPageSurfaces::Get().ForgetAutofillVerify(verify_->prompt);
    }
    verify_.reset();
  }
  if (request_ && request_->frame == frame) {
    Finish(Error("Koan.pageChanged", "the page moved on during the autofill"));
  }
}

void AgentAutofill::RenderFrameDeleted(content::RenderFrameHost* frame) {
  ForgetFrame(frame->GetFrameTreeNodeId());
}

}  // namespace koan::agent
