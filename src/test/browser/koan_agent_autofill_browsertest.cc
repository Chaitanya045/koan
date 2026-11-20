// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// `koan autofill` in the browser (.agent/notes/agent-control.md section 7.0,
// "Autofill for agents"; koan_agent_autofill.h), against a real window and
// Chrome's own autofill and password manager: the suggestions an agent is
// shown, a pick filled through Chrome's fill, a card Chrome must unmask
// asking the user, a login for another site's form refused, the "What agents
// can use" switches, and what the browser then knows of the secrets it
// filled (the facade's lock, the fields it hides, the scrubber's values).
// T-sec-1: whether page script reads a password Koan filled.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/autofill/content/browser/content_autofill_driver.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/data_manager/payments/payments_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager_test_util.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/test_utils/autofill_test_util.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/password_manager/core/browser/password_string.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/agent/koan_agent_autofill.h"
#include "koan/browser/agent/koan_agent_autofill_hooks.h"
#include "koan/browser/agent/koan_agent_page_surfaces.h"
#include "koan/browser/agent/koan_agent_prompts.h"
#include "koan/browser/containers/koan_agent_access_prefs.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "url/gurl.h"

namespace koan {

namespace {

using agent::AgentAutofill;
using agent::FacadeBackend;

constexpr char kPassword[] = "c0rrect-h0rse";
constexpr char16_t kPassword16[] = u"c0rrect-h0rse";

std::unique_ptr<net::test_server::HttpResponse> ServePage(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  const std::string path(request.GetURL().path());
  if (path == "/address") {
    response->set_content(
        "<!doctype html><form>"
        "<input id='name' autocomplete='name'>"
        "<input id='street' autocomplete='address-line1'>"
        "<input id='city' autocomplete='address-level2'>"
        "<input id='zip' autocomplete='postal-code'>"
        "</form>");
  } else if (path == "/card") {
    response->set_content(
        "<!doctype html><form>"
        "<input id='ccname' autocomplete='cc-name'>"
        "<input id='number' autocomplete='cc-number'>"
        "<input id='exp' autocomplete='cc-exp'>"
        "<input id='cvc' autocomplete='cc-csc'>"
        "</form>");
  } else if (path == "/login" || path == "/elsewhere") {
    const std::string action =
        path == "/login" ? "/signed-in" : "https://other.example/collect";
    response->set_content(
        "<!doctype html><form method='post' action='" + action +
        "'>"
        "<input id='user' name='user' autocomplete='username'>"
        "<input id='pass' name='pass' type='password' "
        "autocomplete='current-password'>"
        "<button>Sign in</button></form>");
  } else {
    response->set_content("<!doctype html><p>ok</p>");
  }
  return response;
}

}  // namespace

class KoanAgentAutofillBrowserTest : public InProcessBrowserTest,
                                     public agent::AutofillHooksDelegate {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // HTTPS: Chrome offers no saved card on a page that isn't
    // (kInsecureContextPaymentDisabledMessage instead).
    https_.RegisterRequestHandler(base::BindRepeating(&ServePage));
    ASSERT_TRUE(https_.Start());
    page_ = browser()->GetTabStripModel()->GetActiveWebContents();
    autofill_ = std::make_unique<AgentAutofill>(
        page_, base::BindRepeating(
                   [](std::vector<std::string>* values,
                      const std::string& value) { values->push_back(value); },
                   &secrets_));
    agent::SetAutofillHooksDelegate(this);
  }

  void TearDownOnMainThread() override {
    agent::SetAutofillHooksDelegate(nullptr);
    autofill_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  // agent::AutofillHooksDelegate:
  bool TakeSuggestions(
      content::WebContents* contents,
      const autofill::AutofillClient::PopupOpenArgs& open_args,
      base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate) override {
    return contents == page_ &&
           autofill_->TakeSuggestions(open_args, std::move(delegate));
  }
  bool RefusesCrossDomainFill(content::WebContents* contents) override {
    return contents == page_ && autofill_->RefusesCrossDomainFill();
  }

 protected:
  autofill::PersonalDataManager& Pdm() {
    return *autofill::PersonalDataManagerFactory::GetForBrowserContext(
        browser()->GetProfile());
  }

  void Open(const std::string& path) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), https_.GetURL(path)));
  }

  // The field's backend DOM node id (its renderer id), once autofill has
  // parsed the form.
  int NodeOf(std::string_view id) {
    int node = 0;
    EXPECT_TRUE(base::test::RunUntil([&] {
      auto* driver = autofill::ContentAutofillDriver::GetForRenderFrameHost(
          page_->GetPrimaryMainFrame());
      if (!driver) {
        return false;
      }
      std::as_const(driver->GetAutofillManager())
          .ForEachCachedForm([&](const autofill::FormStructure& form) {
            for (const auto& field : form.fields()) {
              if (field->id_attribute() == base::UTF8ToUTF16(id)) {
                node = static_cast<int>(field->renderer_id().value());
              }
            }
          });
      return node != 0;
    }));
    return node;
  }

  FacadeBackend::AutofillResult Run(int node,
                                    bool list,
                                    std::optional<int> index = std::nullopt,
                                    std::string label = std::string()) {
    FacadeBackend::AutofillRequest request;
    request.node = node;
    request.list = list;
    request.pick.index = index;
    request.pick.label = std::move(label);
    FacadeBackend::AutofillResult result;
    base::RunLoop loop;
    autofill_->Run(
        page_->GetPrimaryMainFrame(), request, "Test agent",
        base::BindOnce(
            [](FacadeBackend::AutofillResult* out, base::OnceClosure done,
               FacadeBackend::AutofillResult result) {
              *out = std::move(result);
              std::move(done).Run();
            },
            &result, loop.QuitClosure()));
    loop.Run();
    return result;
  }

  std::string ValueOf(std::string_view id) {
    return content::EvalJs(page_, base::StrCat({"document.getElementById('", id,
                                                "').value"}))
        .ExtractString();
  }

  void SaveLogin() {
    password_manager::StoredCredential form;
    form.scheme = password_manager::PasswordForm::Scheme::kHtml;
    form.signon_realm = https_.base_url().spec();
    form.url = https_.GetURL("/login");
    form.username_value = u"ada@example.com";
    form.password_value =
        password_manager::PasswordString(std::u16string(kPassword16));
    base::RunLoop saved;
    ProfilePasswordStoreFactory::GetForProfile(
        browser()->GetProfile(), ServiceAccessType::IMPLICIT_ACCESS)
        ->AddLogin(std::move(form), saved.QuitClosure());
    saved.Run();
  }

  // Marks the tab an agent's own ("Your logins" on), so that the "What
  // agents can use" switches apply to it.
  void MakeAgentTab() {
    containers::SetAgentTab(
        page_, containers::ContainerId{containers::ContainerKind::kAgent,
                                       containers::NewContainerId(false)});
  }

  net::EmbeddedTestServer https_{net::EmbeddedTestServer::TYPE_HTTPS};
  raw_ptr<content::WebContents> page_ = nullptr;
  std::unique_ptr<AgentAutofill> autofill_;
  std::vector<std::string> secrets_;
};

// T-fill-1: an address form lists the user's addresses as labels and fills
// the one picked through Chrome's own fill.
IN_PROC_BROWSER_TEST_F(KoanAgentAutofillBrowserTest, AnAddress) {
  {
    autofill::PersonalDataChangedWaiter waiter(Pdm());
    Pdm().address_data_manager().AddProfile(autofill::test::GetFullProfile());
    std::move(waiter).Wait();
  }
  Open("/address");
  const int node = NodeOf("name");
  FacadeBackend::AutofillResult listed = Run(node, /*list=*/true);
  ASSERT_EQ(listed.error_code, "") << listed.error_message;
  const base::ListValue* rows = listed.result.FindList("suggestions");
  ASSERT_TRUE(rows);
  ASSERT_GE(rows->size(), 1u);
  EXPECT_EQ(*(*rows)[0].GetDict().FindString("kind"), "address");

  FacadeBackend::AutofillResult picked = Run(node, false, 1);
  ASSERT_EQ(picked.error_code, "") << picked.error_message;
  EXPECT_EQ(picked.result.FindBool("filled"), true);
  EXPECT_TRUE(base::test::RunUntil([&] { return !ValueOf("street").empty(); }));
  // An address is no secret: nothing is locked or remembered.
  EXPECT_FALSE(autofill_->SecretsIn(page_->GetPrimaryMainFrame()).locked);
  EXPECT_TRUE(secrets_.empty());
  EXPECT_EQ(Run(node, false, 9).error_code, "Koan.noSuchSuggestion");
}

// T-fill-2: a card from the user's cards fills, and then everything the
// facade needs to keep it from the agent is known: its frame is locked,
// its fields are hidden and painted over, and its number is the scrubber's.
IN_PROC_BROWSER_TEST_F(KoanAgentAutofillBrowserTest, ALocalCard) {
  const autofill::CreditCard card = autofill::test::GetCreditCard();
  {
    autofill::PersonalDataChangedWaiter waiter(Pdm());
    Pdm().payments_data_manager().AddCreditCard(card);
    std::move(waiter).Wait();
  }
  Open("/card");
  const int node = NodeOf("number");
  FacadeBackend::AutofillResult listed = Run(node, true);
  ASSERT_EQ(listed.error_code, "") << listed.error_message;
  const std::string label = *listed.result.FindList("suggestions")
                                 ->front()
                                 .GetDict()
                                 .FindString("label");
  // Never the number.
  EXPECT_EQ(label.find(base::UTF16ToUTF8(card.number())), std::string::npos)
      << label;

  FacadeBackend::AutofillResult picked = Run(node, false, 1);
  if (picked.result.FindString("userNeeded")) {
    // Mandatory reauth is on on this machine: covered by the next test.
    GTEST_SKIP() << "Chrome asks for Touch ID or the device password here";
  }
  ASSERT_EQ(picked.error_code, "") << picked.error_message;
  EXPECT_TRUE(base::test::RunUntil([&] { return !ValueOf("number").empty(); }));
  const FacadeBackend::SecretFacts facts =
      autofill_->SecretsIn(page_->GetPrimaryMainFrame());
  EXPECT_TRUE(facts.locked);
  EXPECT_TRUE(facts.card_filled);
  EXPECT_TRUE(facts.fields.contains(node));
  EXPECT_FALSE(autofill_->CardFields().nodes.empty());
  EXPECT_TRUE(std::ranges::find(secrets_, base::UTF16ToUTF8(card.number())) !=
              secrets_.end());

  // The page moves on: the lock goes, the values stay for the tab's life.
  Open("/other");
  EXPECT_FALSE(autofill_->SecretsIn(page_->GetPrimaryMainFrame()).locked);
  EXPECT_FALSE(autofill_->secret_values().empty());
}

// T-fill-3: a card Chrome asks the user to verify before it fills waits for
// the user: the pick says so, and the indicator holds the prompt. A local
// card with "verify it's you" (mandatory reauth) on: a server card would be
// one too, but Chrome offers none without a payments account (only "save
// and fill").
IN_PROC_BROWSER_TEST_F(KoanAgentAutofillBrowserTest, ACardToVerify) {
  {
    autofill::PersonalDataChangedWaiter waiter(Pdm());
    Pdm().payments_data_manager().AddCreditCard(
        autofill::test::GetCreditCard());
    std::move(waiter).Wait();
  }
  Pdm().payments_data_manager().SetPaymentMethodsMandatoryReauthEnabled(true);
  Open("/card");
  const int node = NodeOf("number");
  FacadeBackend::AutofillResult picked = Run(node, false, 1);
  ASSERT_EQ(picked.error_code, "") << picked.error_message;
  EXPECT_EQ(picked.result.FindBool("filled"), false);
  EXPECT_EQ(*picked.result.FindString("userNeeded"), "autofill-verify");
  bool held = false;
  for (const agent::HeldPrompt& prompt :
       agent::AgentPageSurfaces::Get().Prompts()) {
    held =
        held || (prompt.contents == page_ &&
                 prompt.prompt.surface == agent::PageSurface::kAutofillVerify);
  }
  EXPECT_TRUE(held);
  // Nothing was filled.
  EXPECT_EQ(ValueOf("number"), "");
  // Another pick waits for this one.
  EXPECT_EQ(Run(node, true).error_code, "Koan.userNeeded");
}

// T-fill-4 and T-sec-1: a saved login fills (username and password) through
// the password manager. The page can read the password: Chrome sets the
// field's real value when a login is picked (as when the user picks it), so
// the page can send it. The agent never gets it: its frame is locked to
// script (Koan.secretField) and the scrubber knows the value.
IN_PROC_BROWSER_TEST_F(KoanAgentAutofillBrowserTest, ALoginAndTSec1) {
  SaveLogin();
  // An agent's tab: Chrome fills nothing there on load (the
  // content_password_manager_driver.cc hook). In a tab of the user's it
  // previews the only saved login on load, and then offers no suggestions
  // for the field (AutofillAgent::ShowSuggestions(): a suggested value).
  MakeAgentTab();
  Open("/login");
  const int user = NodeOf("user");
  FacadeBackend::AutofillResult listed;
  // The password manager fetches its matches after the form is seen.
  EXPECT_TRUE(base::test::RunUntil([&] {
    listed = Run(user, true);
    return listed.error_code.empty();
  }));
  const std::string label = *listed.result.FindList("suggestions")
                                 ->front()
                                 .GetDict()
                                 .FindString("label");
  EXPECT_EQ(label, "ada@example.com (saved password)");
  EXPECT_EQ(label.find(kPassword), std::string::npos);

  FacadeBackend::AutofillResult picked = Run(user, false, std::nullopt, "ada");
  if (picked.result.FindString("userNeeded")) {
    GTEST_SKIP() << "Chrome asks for Touch ID before filling here";
  }
  ASSERT_EQ(picked.error_code, "") << picked.error_message;
  EXPECT_EQ(picked.result.FindBool("filled"), true);
  EXPECT_EQ(ValueOf("user"), "ada@example.com");
  // T-sec-1: page script reads it.
  EXPECT_EQ(ValueOf("pass"), kPassword);
  const FacadeBackend::SecretFacts facts =
      autofill_->SecretsIn(page_->GetPrimaryMainFrame());
  EXPECT_TRUE(facts.locked);
  EXPECT_FALSE(facts.card_filled);
  EXPECT_TRUE(facts.fields.contains(NodeOf("pass")));
  EXPECT_EQ(secrets_, (std::vector<std::string>{kPassword}));

  // The page clears the field by script and the user types: still locked
  // only while the value is there.
  ASSERT_TRUE(
      content::ExecJs(page_, "document.getElementById('pass').value=''"));
  ASSERT_TRUE(
      content::ExecJs(page_, "document.getElementById('pass').focus()"));
  content::SimulateKeyPress(page_, ui::DomKey::FromCharacter('x'),
                            ui::DomCode::US_X, ui::VKEY_X, false, false, false,
                            false);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return !autofill_->SecretsIn(page_->GetPrimaryMainFrame()).locked;
  }));
}

// T-fill-5: a form that sends what is typed to another site gets no login.
IN_PROC_BROWSER_TEST_F(KoanAgentAutofillBrowserTest, AFormSentElsewhere) {
  SaveLogin();
  MakeAgentTab();  // As in ALoginAndTSec1.
  Open("/elsewhere");
  const int user = NodeOf("user");
  FacadeBackend::AutofillResult result;
  EXPECT_TRUE(base::test::RunUntil([&] {
    result = Run(user, false, 1);
    return result.error_code != "Koan.noSuggestions";
  }));
  EXPECT_EQ(result.error_code, "Koan.offSite");
  EXPECT_EQ(ValueOf("pass"), "");
  EXPECT_TRUE(secrets_.empty());
}

// T-fill-6: with "Addresses and cards" or "Saved passwords" off, an agent's
// tab gets no suggestions, and the agent is told why.
IN_PROC_BROWSER_TEST_F(KoanAgentAutofillBrowserTest, TheSwitches) {
  {
    autofill::PersonalDataChangedWaiter waiter(Pdm());
    Pdm().address_data_manager().AddProfile(autofill::test::GetFullProfile());
    std::move(waiter).Wait();
  }
  SaveLogin();
  MakeAgentTab();
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetBoolean(containers::kAgentMayUseAutofill, false);
  Open("/address");
  EXPECT_EQ(Run(NodeOf("name"), true).error_code, "Koan.autofillOff");
  prefs->SetBoolean(containers::kAgentMayUseAutofill, true);
  EXPECT_EQ(Run(NodeOf("name"), true).error_code, "");

  // No saved password is offered (an address may be, for the username).
  prefs->SetBoolean(containers::kAgentMayUsePasswords, false);
  Open("/login");
  FacadeBackend::AutofillResult login = Run(NodeOf("user"), true);
  if (login.error_code.empty()) {
    for (const base::Value& row : *login.result.FindList("suggestions")) {
      EXPECT_NE(*row.GetDict().FindString("kind"), "password");
    }
  } else {
    EXPECT_EQ(login.error_code, "Koan.autofillOff");
  }
}

// The pick works only on what Chrome's autofill knows as a field.
IN_PROC_BROWSER_TEST_F(KoanAgentAutofillBrowserTest, NotAField) {
  Open("/other");
  EXPECT_EQ(Run(12345, true).error_code, "Koan.notAField");
}

}  // namespace koan
