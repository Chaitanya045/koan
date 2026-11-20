// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// What auto-resume (.agent/notes/agent-control.md section 9.2) reads from the
// browser about a paused tab (koan_agent_pause_watch.h), against a real
// window: whether the user is on the tab once another tab is shown, a
// password or card field with focus holding the pause, whether the page
// changed during the pause, and the timer that moves the policy on.

#include <memory>
#include <optional>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/agent/koan_agent_auto_resume.h"
#include "koan/browser/agent/koan_agent_pause_watch.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "url/gurl.h"

namespace koan {

namespace {

using agent::AutoResumeSettings;
using agent::PauseHold;
using agent::PauseWatch;

std::unique_ptr<net::test_server::HttpResponse> ServeForm(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(
      "<!doctype html><title>Form</title><form>"
      "<input id='name' autocomplete='name'>"
      "<input id='pass' type='password'>"
      "<input id='card' autocomplete='cc-number'>"
      "</form><p id='text'>before</p>");
  return response;
}

}  // namespace

class KoanAgentPauseWatchBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeForm));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/form")));
    page_ = browser()->GetTabStripModel()->GetActiveWebContents();
    watch_ = std::make_unique<PauseWatch>(
        page_, base::BindRepeating([](int* ticks) { ++*ticks; }, &ticks_),
        base::DoNothing());
  }

  void TearDownOnMainThread() override {
    watch_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  void Focus(const char* id) {
    ASSERT_TRUE(content::ExecJs(
        page_, std::string("document.getElementById('") + id + "').focus()"));
  }

  bool Changed() {
    std::optional<bool> changed;
    base::RunLoop loop;
    watch_->QueryChanged(base::BindOnce(
        [](std::optional<bool>* out, base::OnceClosure done, bool value) {
          *out = value;
          std::move(done).Run();
        },
        &changed, loop.QuitClosure()));
    loop.Run();
    return changed.value_or(true);
  }

  raw_ptr<content::WebContents> page_ = nullptr;
  std::unique_ptr<PauseWatch> watch_;
  int ticks_ = 0;
};

// Rule 2: another tab shown is the user leaving the tab.
IN_PROC_BROWSER_TEST_F(KoanAgentPauseWatchBrowserTest, AnotherTabIsLeaving) {
  // (A test window is not always the active one: only then is the user on
  // the shown tab.)
  if (browser()->IsActive()) {
    EXPECT_TRUE(watch_->IsUserOnTab());
  }
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("about:blank"), WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  EXPECT_FALSE(watch_->IsUserOnTab());
}

// Rule 1: a password or card field with focus holds the pause.
IN_PROC_BROWSER_TEST_F(KoanAgentPauseWatchBrowserTest, SecretFieldsHold) {
  Focus("name");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return watch_->CurrentHold() == PauseHold::kNone; }));
  Focus("pass");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return watch_->CurrentHold() == PauseHold::kSecretField; }));
  Focus("name");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return watch_->CurrentHold() == PauseHold::kNone; }));
  // autocomplete="cc-number", through autofill's view of the focused field.
  Focus("card");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return watch_->CurrentHold() == PauseHold::kSecretField; }));
}

// Rule 5: `changed` is a DOM or form-value change during the pause.
IN_PROC_BROWSER_TEST_F(KoanAgentPauseWatchBrowserTest, PageChanges) {
  watch_->Begin("user-input", AutoResumeSettings());
  EXPECT_FALSE(Changed());
  watch_->End();

  watch_->Begin("user-input", AutoResumeSettings());
  ASSERT_TRUE(content::ExecJs(
      page_, "document.getElementById('text').textContent = 'after'"));
  EXPECT_TRUE(Changed());
  watch_->End();

  // Typing in a field changes no DOM node: the input event says it.
  watch_->Begin("user-input", AutoResumeSettings());
  Focus("name");
  content::SimulateKeyPress(page_, ui::DomKey::FromCharacter('a'),
                            ui::DomCode::US_A, ui::VKEY_A, false, false, false,
                            false);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page_, "document.getElementById('name').value")
               .ExtractString() == "a";
  }));
  EXPECT_TRUE(Changed());
  watch_->End();
}

// The timer moves a pause that may resume by itself on, and stops for a
// held one.
IN_PROC_BROWSER_TEST_F(KoanAgentPauseWatchBrowserTest, TimerRunsUntilHeld) {
  watch_->Begin("user-input",
                AutoResumeSettings{.enabled = true, .delay = base::Seconds(2)});
  EXPECT_TRUE(base::test::RunUntil([&] { return ticks_ > 0; }));
  watch_->policy().Hold(PauseHold::kKeepPaused);
  watch_->Rearm();
  const int ticks = ticks_;
  base::RunLoop loop;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Milliseconds(1500));
  loop.Run();
  EXPECT_EQ(ticks_, ticks);
  watch_->End();
  EXPECT_FALSE(watch_->policy().paused());
}

}  // namespace koan
