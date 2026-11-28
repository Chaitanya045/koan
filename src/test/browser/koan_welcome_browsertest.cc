// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The welcome's data never waits forever (#493): on Linux, a fresh profile
// on a machine with other browsers stayed on "Get started", because the page
// gets nothing until the import detection and the search engines are ready.
// Here the detection blocks (upstream's Firefox detection reads a
// profiles.ini that is a FIFO, which never ends until a writer comes): the
// page is answered within KoanWelcomeHandler::kWelcomeDataTimeout, with no
// browsers yet, and takes the list when the detection ends. Fixture folders
// only, under --koan-import-test-root and a test XDG_CONFIG_HOME. Linux
// only (src/test/BUILD.gn): on macOS upstream's Firefox folder can't be
// moved to a fixture.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/posix/eintr_wrapper.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/koan_switches.h"
#include "koan/browser/ui/webui/koan_welcome_handler.h"
#include "koan/browser/ui/webui/koan_welcome_host.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan {

namespace {

// The step the page shows, or "" before its steps are made.
constexpr char kStepScript[] =
    "document.getElementById('welcome-card')?.getAttribute('step') ?? ''";
// The import step's choices, by title.
constexpr char kChoicesScript[] =
    "[...document.querySelectorAll('input[name=import]')].map("
    "  i => i.closest('label').querySelector('.choice-title').textContent)"
    ".join('|')";
constexpr char kNextScript[] =
    "document.querySelector('#welcome-card .step-foot .button.primary')"
    ".click()";

class KoanWelcomeBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    ASSERT_TRUE(root_.CreateUniqueTempDir());
    command_line->AppendSwitchPath(switches::kImportTestRoot, root_.GetPath());
    // Upstream's Firefox detection looks in $XDG_CONFIG_HOME/mozilla first.
    ASSERT_TRUE(base::Environment::Create()->SetVar(
        "XDG_CONFIG_HOME", root_.GetPath().AppendASCII("xdg").value()));
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    const base::FilePath chrome =
        root_.GetPath().AppendASCII(".config/google-chrome");
    fifo_ = root_.GetPath().AppendASCII("xdg/mozilla/firefox/profiles.ini");
    ASSERT_TRUE(base::CreateDirectory(chrome.AppendASCII("Default")));
    ASSERT_TRUE(base::WriteFile(chrome.AppendASCII("Default/History"), ""));
    ASSERT_TRUE(base::CreateDirectory(fifo_.DirName()));
    ASSERT_EQ(0, mkfifo(fifo_.value().c_str(), 0600));
  }

  void TearDownOnMainThread() override {
    // Never leave the detection's thread blocked.
    Release();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  // Lets a read of the FIFO end (empty), once one has started.
  bool Release() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    const int fd =
        HANDLE_EINTR(open(fifo_.value().c_str(), O_WRONLY | O_NONBLOCK));
    if (fd < 0) {
      return false;
    }
    IGNORE_EINTR(close(fd));
    return true;
  }

  // The welcome as the first-run screen shows it: the page with a host.
  content::WebContents* OpenWelcome() {
    content::WebContents* contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    KoanWelcomeHost::CreateForWebContents(contents,
                                          KoanWelcomeHost::Callbacks());
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                             GURL("chrome://koan-welcome/")));
    return contents;
  }

  static std::string Eval(content::WebContents* contents, const char* script) {
    return content::EvalJs(contents, script).ExtractString();
  }

  base::ScopedTempDir root_;
  base::FilePath fifo_;
};

}  // namespace

// The detection blocks: the page still makes its steps, within the timeout,
// with "Import from another browser…" for a list it doesn't have yet; the
// list comes when the detection ends, Koan's browser in it.
IN_PROC_BROWSER_TEST_F(KoanWelcomeBrowserTest, NeverWaitsForTheDetection) {
  const base::TimeTicks start = base::TimeTicks::Now();
  content::WebContents* welcome = OpenWelcome();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Eval(welcome, kStepScript) == "welcome"; }));
  EXPECT_LT(base::TimeTicks::Now() - start,
            KoanWelcomeHandler::kWelcomeDataTimeout + base::Seconds(10));

  // Get started to the import step (no shortcuts step on Linux).
  ASSERT_TRUE(base::test::RunUntil([&] {
    if (Eval(welcome, kStepScript) == "import") {
      return true;
    }
    EXPECT_TRUE(content::ExecJs(welcome, kNextScript));
    return false;
  }));
  EXPECT_EQ("Start fresh|Import from another browser\u2026",
            Eval(welcome, kChoicesScript));

  // The detection ends: the step shows the browsers found.
  ASSERT_TRUE(base::test::RunUntil([&] { return Release(); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Eval(welcome, kChoicesScript).find("Google Chrome") !=
           std::string::npos;
  }));
  EXPECT_EQ("import", Eval(welcome, kStepScript));
}

}  // namespace koan
