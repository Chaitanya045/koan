// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// A password Koan did not fill, in a user's tab an agent drives (#412 review,
// S1; .agent/notes/agent-control.md section 4.5, T-sec-18 and T-sec-19): the
// password manager filled it on load and the user's click showed it to the
// page, or the user typed it, and then handed the tab back. The agent's
// script is refused (Koan.secretField), what it reads of the page hides the
// value, and a copy of it the page or the server makes reads "[secret]".
//
// The whole path an agent's messages take: an AgentForwarder with one
// connected client (as koan-relay or the `koan` CLI is), the take-over
// prompt answered, the tab's real DevTools session, and the user's real
// input (key and mouse events without kFromDebugger).

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/password_manager/core/browser/password_string.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "koan/browser/agent/koan_agent_forwarder.h"
#include "koan/browser/agent/koan_agent_registry.h"
#include "koan/browser/agent/koan_agent_service.h"
#include "koan/browser/agent/koan_agent_session.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "url/gurl.h"

namespace koan {

namespace {

using agent::AgentForwarder;
using agent::AgentMessage;
using agent::TabState;

constexpr uint64_t kConnection = 1;
constexpr char kClient[] = "test-client";
// The saved login's password, and one the user types (letters only: typed
// key by key).
constexpr char kSavedPassword[] = "c0rrect-h0rse";
constexpr char16_t kSavedPassword16[] = u"c0rrect-h0rse";
constexpr char kTypedPassword[] = "tangerinestaple";

// /login: a form that posts to /signed-in, whose page echoes what it was
// sent; the login page mirrors what is typed into the password field into
// its `value` attribute and into a text node, as some pages do.
std::unique_ptr<net::test_server::HttpResponse> ServePage(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  const std::string path(request.GetURL().path());
  if (path == "/login") {
    response->set_content(
        "<!doctype html><form method='post' action='/signed-in'>"
        "<input id='user' name='user' autocomplete='username'>"
        "<input id='pass' name='pass' type='password' "
        "autocomplete='current-password' "
        "oninput=\"this.setAttribute('value', this.value); "
        "document.getElementById('mirror').textContent = this.value\">"
        "<button id='go'>Sign in</button></form>"
        "<p id='mirror'></p>");
  } else if (path == "/signed-in") {
    response->set_content("<!doctype html><p id='echo'>" + request.content +
                          "</p>");
  } else {
    response->set_content("<!doctype html><p>ok</p>");
  }
  return response;
}

}  // namespace

class KoanAgentSecretLockBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
    {
      base::ScopedAllowBlockingForTesting allow;
      ASSERT_TRUE(log_dir_.CreateUniqueTempDir());
    }
    forwarder_ = std::make_unique<AgentForwarder>(
        base::BindRepeating(&KoanAgentSecretLockBrowserTest::OnSend,
                            base::Unretained(this)),
        log_dir_.GetPath(), base::DoNothing());
    forwarder_->OnClientConnected(kConnection, kClient, "Test agent");
    page_ = browser()->GetTabStripModel()->GetActiveWebContents();
  }

  void TearDownOnMainThread() override {
    forwarder_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  void OnSend(uint64_t connection_id, std::string json) {
    EXPECT_EQ(connection_id, kConnection);
    received_.push_back(std::move(json));
  }

  int Send(std::string method,
           base::DictValue params = base::DictValue(),
           std::optional<std::string> session = std::nullopt) {
    AgentMessage message;
    message.id = next_id_++;
    message.method = std::move(method);
    message.params = std::move(params);
    message.session_id = std::move(session);
    message.client_id = kClient;
    message.capabilities.observe = true;
    message.capabilities.read = true;
    message.capabilities.drive = true;
    const int id = message.id;
    forwarder_->OnClientMessage(kConnection, std::move(message));
    return id;
  }

  base::DictValue WaitForAnswer(int id) {
    base::DictValue answer;
    EXPECT_TRUE(base::test::RunUntil([&] {
      for (const std::string& json : received_) {
        std::optional<base::DictValue> message =
            base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
        if (message && message->FindInt("id") == id) {
          answer = std::move(*message);
          return true;
        }
      }
      return false;
    })) << "no answer to "
        << id;
    return answer;
  }

  base::DictValue Call(std::string method,
                       base::DictValue params = base::DictValue(),
                       std::optional<std::string> session = std::nullopt) {
    return WaitForAnswer(Send(std::move(method), std::move(params), session));
  }

  static std::string ErrorOf(const base::DictValue& answer) {
    const std::string* code = answer.FindStringByDottedPath("error.data");
    return code ? *code : std::string();
  }

  // The user gives the agent their tab (the prompt's Allow), and the agent
  // attaches to it.
  void TakeOver() {
    target_id_ =
        content::DevToolsAgentHost::GetOrCreateFor(page_.get())->GetId();
    const int id =
        Send("Koan.takeOver", base::DictValue().Set("targetId", target_id_));
    ASSERT_TRUE(base::test::RunUntil(
        [] { return agent::AnswerTakeOverDialogsForTesting(true) > 0; }));
    ASSERT_EQ(ErrorOf(WaitForAnswer(id)), "");
    base::DictValue attached = Call(
        "Target.attachToTarget",
        base::DictValue().Set("targetId", target_id_).Set("flatten", true));
    const std::string* session =
        attached.FindStringByDottedPath("result.sessionId");
    ASSERT_TRUE(session) << *base::WriteJson(attached);
    session_ = *session;
  }

  const agent::TargetEntry* Entry() const {
    return forwarder_->registry().Find(target_id_);
  }

  // The user's input paused the tab; they hand it back.
  void HandBack() {
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return Entry() && Entry()->state == TabState::kPaused; }));
    forwarder_->HandBack(target_id_);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return Entry() && Entry()->state == TabState::kDriven; }));
  }

  std::string PageValue(std::string_view id) {
    return content::EvalJs(
               page_.get(),
               base::StrCat({"document.getElementById('", id, "').value"}))
        .ExtractString();
  }

  void SaveLogin() {
    password_manager::StoredCredential form;
    form.scheme = password_manager::PasswordForm::Scheme::kHtml;
    form.signon_realm = embedded_test_server()->base_url().spec();
    form.url = embedded_test_server()->GetURL("/login");
    form.username_value = u"ada@example.com";
    form.password_value =
        password_manager::PasswordString(std::u16string(kSavedPassword16));
    base::RunLoop saved;
    ProfilePasswordStoreFactory::GetForProfile(
        browser()->GetProfile(), ServiceAccessType::IMPLICIT_ACCESS)
        ->AddLogin(std::move(form), saved.QuitClosure());
    saved.Run();
  }

  // What the agent tries once the tab is back, with `password` in the
  // page: every script read is refused, the DOM and accessibility answers
  // hide it, and after the agent submits the form, neither the request nor
  // the page that echoes it gives it away.
  void ExpectTheAgentNeverReads(const std::string& password,
                                bool page_mirrored) {
    base::DictValue evaluated =
        Call("Runtime.evaluate",
             base::DictValue().Set("expression",
                                   "document.getElementById('pass').value"),
             session_);
    EXPECT_EQ(ErrorOf(evaluated), "Koan.secretField")
        << *base::WriteJson(evaluated);

    base::DictValue document =
        Call("DOM.getDocument", base::DictValue(), session_);
    const std::optional<int> root =
        document.FindIntByDottedPath("result.root.nodeId");
    ASSERT_TRUE(root) << *base::WriteJson(document);
    base::DictValue found =
        Call("DOM.querySelector",
             base::DictValue().Set("nodeId", *root).Set("selector", "#pass"),
             session_);
    const std::optional<int> field = found.FindIntByDottedPath("result.nodeId");
    ASSERT_TRUE(field) << *base::WriteJson(found);
    base::DictValue resolved = Call(
        "DOM.resolveNode", base::DictValue().Set("nodeId", *field), session_);
    const std::string* object =
        resolved.FindStringByDottedPath("result.object.objectId");
    ASSERT_TRUE(object) << *base::WriteJson(resolved);
    base::DictValue called = Call(
        "Runtime.callFunctionOn",
        base::DictValue()
            .Set("objectId", *object)
            .Set("functionDeclaration", "function() { return this.value; }")
            .Set("returnByValue", true),
        session_);
    EXPECT_EQ(ErrorOf(called), "Koan.secretField") << *base::WriteJson(called);

    // The snapshot and the page's HTML (where the page copied the value
    // into the field's `value` attribute and a text node).
    const std::string tree = *base::WriteJson(
        Call("Accessibility.getFullAXTree", base::DictValue(), session_));
    const std::string html = *base::WriteJson(Call(
        "DOM.getOuterHTML", base::DictValue().Set("nodeId", *root), session_));
    const std::string attributes = *base::WriteJson(
        Call("DOM.getAttributes", base::DictValue().Set("nodeId", *field),
             session_));
    const std::string described = *base::WriteJson(Call(
        "DOM.describeNode", base::DictValue().Set("nodeId", *field), session_));
    for (const std::string* answer : {&tree, &html, &attributes, &described}) {
      EXPECT_EQ(answer->find(password), std::string::npos) << *answer;
    }
    if (page_mirrored) {
      EXPECT_NE(html.find("[secret]"), std::string::npos) << html;
    }

    // The agent signs in (Enter in the field the user left focused); the
    // server echoes the body it got.
    ASSERT_EQ(ErrorOf(Call("Network.enable", base::DictValue(), session_)), "");
    content::TestNavigationObserver signed_in(page_.get());
    for (const char* type : {"keyDown", "keyUp"}) {
      Call("Input.dispatchKeyEvent",
           base::DictValue()
               .Set("type", type)
               .Set("key", "Enter")
               .Set("code", "Enter")
               .Set("windowsVirtualKeyCode", 13)
               .Set("text", std::string_view(type) == "keyDown" ? "\r" : ""),
           session_);
    }
    signed_in.Wait();
    ASSERT_EQ(page_->GetLastCommittedURL().path(), "/signed-in");
    ASSERT_NE(content::EvalJs(page_.get(),
                              "document.getElementById('echo').textContent")
                  .ExtractString()
                  .find(password),
              std::string::npos)
        << "the server got the password";

    // The login POST: its body never reaches the agent from the Network
    // domain (the events drop it, the method is gated), and wherever a copy
    // goes, it reads "[secret]".
    std::string request_id;
    ASSERT_TRUE(base::test::RunUntil([&] {
      for (const std::string& json : received_) {
        std::optional<base::DictValue> message =
            base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
        const std::string* method =
            message ? message->FindString("method") : nullptr;
        const std::string* url =
            message ? message->FindStringByDottedPath("params.request.url")
                    : nullptr;
        if (method && *method == "Network.requestWillBeSent" && url &&
            GURL(*url).path() == "/signed-in") {
          request_id = *message->FindStringByDottedPath("params.requestId");
          return true;
        }
      }
      return false;
    }));
    const std::string post_data = *base::WriteJson(
        Call("Network.getRequestPostData",
             base::DictValue().Set("requestId", request_id), session_));
    EXPECT_EQ(post_data.find(password), std::string::npos) << post_data;
    base::DictValue echo = Call(
        "Runtime.evaluate",
        base::DictValue()
            .Set("expression", "document.getElementById('echo').textContent")
            .Set("returnByValue", true),
        session_);
    const std::string* echoed =
        echo.FindStringByDottedPath("result.result.value");
    ASSERT_TRUE(echoed) << *base::WriteJson(echo);
    EXPECT_NE(echoed->find("pass=[secret]"), std::string::npos) << *echoed;

    // Nothing the client got at all carries it.
    for (const std::string& json : received_) {
      EXPECT_EQ(json.find(password), std::string::npos) << json;
    }
  }

  base::ScopedTempDir log_dir_;
  std::unique_ptr<AgentForwarder> forwarder_;
  raw_ptr<content::WebContents> page_ = nullptr;
  std::vector<std::string> received_;
  int next_id_ = 1;
  std::string target_id_;
  std::string session_;
};

// T-sec-18: the password manager filled the saved login when the page loaded
// (Chrome shows it to the page only after the user's first click), in a tab
// the agent took over.
IN_PROC_BROWSER_TEST_F(KoanAgentSecretLockBrowserTest,
                       APasswordFilledOnLoadInAUsersTab) {
  SaveLogin();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/login")));
  TakeOver();
  // The user clicks into the password field: the fill is the page's now.
  content::SimulateMouseClickOrTapElementWithId(page_.get(), "pass");
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(10);
  while (PageValue("pass") != kSavedPassword) {
    if (base::TimeTicks::Now() > deadline) {
      GTEST_SKIP() << "the password manager does not fill on load here";
    }
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(100));
    loop.Run();
  }
  HandBack();
  ExpectTheAgentNeverReads(kSavedPassword, /*page_mirrored=*/false);
}

// T-sec-19: the user typed the password themselves (real key events) and
// handed the tab back.
IN_PROC_BROWSER_TEST_F(KoanAgentSecretLockBrowserTest,
                       APasswordTheUserTypedInAUsersTab) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/login")));
  TakeOver();
  content::SimulateMouseClickOrTapElementWithId(page_.get(), "pass");
  for (char c : std::string_view(kTypedPassword)) {
    const auto key_code = static_cast<ui::KeyboardCode>(ui::VKEY_A + (c - 'a'));
    content::SimulateKeyPress(page_.get(), ui::DomKey::FromCharacter(c),
                              ui::UsLayoutKeyboardCodeToDomCode(key_code),
                              key_code, false, false, false, false);
  }
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return PageValue("pass") == kTypedPassword; }));
  HandBack();
  ExpectTheAgentNeverReads(kTypedPassword, /*page_mirrored=*/true);
}

}  // namespace koan
