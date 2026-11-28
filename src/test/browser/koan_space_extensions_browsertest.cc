// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Extensions per Space profile (#473,
// .agent/notes/upstream/space-extensions.md): an extension that is off in a
// Space's profile injects nothing into its tabs, sees none of their requests
// (webRequest, declarativeNetRequest; subresources and navigations), doesn't
// see the tabs (tabs.query, tab and webNavigation events), can't address them
// (tab ids, the active tab) and has no context menu item in them, and still
// works in every other Space. A change applies to the profile's open tabs by
// restarting them.

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/files/file_path.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/context_menu_matcher.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/background_script_executor.h"
#include "extensions/common/extension.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/test_extension_dir.h"
#include "koan/browser/containers/koan_container_extensions.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_space_extensions.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace koan {

namespace {

// A content script that marks the page, a webRequest listener that keeps the
// requests it sees (type and URL), webNavigation.onCommitted and
// tabs.onUpdated listeners that keep the URLs they get, a context menu item,
// and a declarativeNetRequest rule that blocks "koan-blocked" (fetches, page
// loads, iframes).
constexpr char kManifest[] = R"({
  "name": "Koan Space extensions test",
  "version": "1",
  "manifest_version": 3,
  "permissions": ["webRequest", "declarativeNetRequest", "scripting", "tabs",
                  "webNavigation", "contextMenus"],
  "host_permissions": ["<all_urls>"],
  "background": {"service_worker": "background.js"},
  "content_scripts": [{
    "matches": ["<all_urls>"],
    "js": ["content.js"],
    "run_at": "document_end"
  }],
  "declarative_net_request": {
    "rule_resources": [{"id": "rules", "enabled": true, "path": "rules.json"}]
  }
})";
constexpr char kBackground[] = R"(
  self.seen = [];
  self.committed = [];
  self.updated = [];
  chrome.webRequest.onBeforeRequest.addListener(
      (details) => { self.seen.push(details.type + ' ' + details.url); },
      {urls: ['<all_urls>']});
  chrome.webNavigation.onCommitted.addListener(
      (details) => { self.committed.push(details.url); });
  chrome.tabs.onUpdated.addListener(
      (id, info, tab) => { self.updated.push(tab.url || ''); });
  chrome.contextMenus.create(
      {id: 'koan-item', title: 'Koan test item', contexts: ['all']},
      () => { chrome.runtime.lastError; chrome.test.sendMessage('ready'); });
)";
constexpr char kContentScript[] =
    "document.documentElement.dataset.koanExtension = 'injected';";
constexpr char kRules[] = R"([{
  "id": 1,
  "priority": 1,
  "action": {"type": "block"},
  "condition": {"urlFilter": "koan-blocked",
                "resourceTypes": ["xmlhttprequest", "main_frame", "sub_frame"]}
}])";

class KoanSpaceExtensionsBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  Profile* profile() { return browser()->GetProfile(); }

  // A new profile (New Profile...), Serialize()d.
  std::string AddProfile(const std::u16string& name) {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    std::optional<containers::UserContainer> container =
        containers::AddUserContainer(update.Get(), name);
    return container ? containers::Serialize(container->id) : std::string();
  }

  // What the dialog's Done writes.
  void SetOff(const std::string& container, bool off) {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    containers::SetExtensionsOff(
        update.Get(), *containers::SpaceContainerFromString(container),
        off ? std::set<std::string>{extension_id_} : std::set<std::string>());
  }

  void LoadTestExtension() {
    dir_.WriteManifest(kManifest);
    dir_.WriteFile(FILE_PATH_LITERAL("background.js"), kBackground);
    dir_.WriteFile(FILE_PATH_LITERAL("content.js"), kContentScript);
    dir_.WriteFile(FILE_PATH_LITERAL("rules.json"), kRules);
    ExtensionTestMessageListener ready("ready");
    scoped_refptr<const extensions::Extension> extension =
        extensions::ChromeTestExtensionLoader(profile()).LoadExtension(
            dir_.UnpackedPath());
    ASSERT_TRUE(extension);
    extension_id_ = extension->id();
    ASSERT_TRUE(ready.WaitUntilSatisfied());
  }

  // Opens `path` in a new tab of the active Space, as Cmd+T and the URL bar
  // do (its profile's partition).
  content::WebContents* OpenInActiveSpace(const std::string& path) {
    NavigateParams params(browser(), embedded_test_server()->GetURL(path),
                          ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::kNewForegroundTab;
    ui_test_utils::NavigateToURL(&params);
    return params.navigated_or_inserted_contents;
  }

  // The extension's content script ran in the page.
  bool Injected(content::WebContents* contents) {
    return content::EvalJs(contents,
                           "document.documentElement.dataset.koanExtension "
                           "|| 'none'")
               .ExtractString() == "injected";
  }

  // The page's fetch of a URL the extension's rule blocks.
  std::string FetchBlockedUrl(content::WebContents* contents) {
    return content::EvalJs(contents,
                           "fetch('/title2.html?koan-blocked').then("
                           "() => 'loaded', () => 'blocked')")
        .ExtractString();
  }

  // Whether a page load of `path` (with "koan-blocked": the extension's rule
  // blocks it) commits a page and not an error page.
  bool PageLoads(content::WebContents* contents, const std::string& path) {
    return content::NavigateToURL(contents,
                                  embedded_test_server()->GetURL(path));
  }

  // The same for an iframe of `path` that the page adds: "loaded" or
  // "blocked" (an error page, which the page can't read).
  std::string IframeLoads(content::WebContents* contents,
                          const std::string& path) {
    return content::EvalJs(
               contents,
               base::StrCat(
                   {"new Promise((resolve) => {"
                    "  const frame = document.createElement('iframe');"
                    "  frame.onload = () => {"
                    "    let doc = null;"
                    "    try { doc = frame.contentDocument; } catch (e) {}"
                    "    resolve(doc && doc.title ? 'loaded' : 'blocked');"
                    "  };"
                    "  frame.src = '",
                    path,
                    "';"
                    "  document.body.appendChild(frame);"
                    "})"}))
        .ExtractString();
  }

  base::Value RunInBackground(const std::string& script) {
    return extensions::BackgroundScriptExecutor::ExecuteScript(
        profile(), extension_id_, script,
        extensions::BackgroundScriptExecutor::ResultCapture::kSendScriptResult);
  }

  // Whether the extension's webRequest listener saw a URL with `marker`.
  bool Saw(const std::string& marker) {
    return RunInBackground(base::StrCat({"chrome.test.sendScriptResult("
                                         "self.seen.some(u => u.includes('",
                                         marker, "')));"}))
        .GetIfBool()
        .value_or(false);
  }

  // Whether `list` (self.seen, self.committed, self.updated) has an entry with
  // `marker`.
  bool Has(const std::string& list, const std::string& marker) {
    return RunInBackground(
               base::StrCat({"chrome.test.sendScriptResult(self.", list,
                             ".some(u => u.includes('", marker, "')));"}))
        .GetIfBool()
        .value_or(false);
  }

  // Whether chrome.tabs.query({}) lists the tab.
  bool Listed(content::WebContents* contents) {
    const int id = extensions::ExtensionTabUtil::GetTabId(contents);
    base::Value ids = RunInBackground(
        "chrome.tabs.query({}).then("
        "(tabs) => chrome.test.sendScriptResult(tabs.map((tab) => tab.id)));");
    if (!ids.is_list()) {
      return false;
    }
    for (const base::Value& listed : ids.GetList()) {
      if (listed.GetIfInt() == id) {
        return true;
      }
    }
    return false;
  }

  // What chrome.tabs.getZoom() without a tab id (the active tab) gives the
  // extension: "ok" or "error".
  std::string ActiveTabCall() {
    base::Value result = RunInBackground(
        "chrome.tabs.getZoom().then("
        "() => chrome.test.sendScriptResult('ok'),"
        "() => chrome.test.sendScriptResult('error'));");
    return result.is_string() ? result.GetString() : std::string();
  }

  // Whether the page's context menu has an item of the extension.
  bool HasMenuItem(content::WebContents* contents) {
    std::unique_ptr<TestRenderViewContextMenu> menu =
        TestRenderViewContextMenu::Create(contents,
                                          contents->GetLastCommittedURL());
    menu->Init();
    return !menu->extension_items().extension_item_map().empty();
  }

  // What chrome.tabs.get() and chrome.scripting.executeScript() of the tab
  // give the extension: "ok" or "error".
  std::string CallWithTabId(content::WebContents* contents,
                            const std::string& call) {
    const std::string id =
        base::NumberToString(extensions::ExtensionTabUtil::GetTabId(contents));
    base::Value result = RunInBackground(
        base::StrCat({"(async () => { try { await ", call, "(", id,
                      "); chrome.test.sendScriptResult('ok'); } catch (e) { "
                      "chrome.test.sendScriptResult('error'); } })();"}));
    return result.is_string() ? result.GetString() : std::string();
  }
  std::string TabsGet(content::WebContents* contents) {
    return CallWithTabId(contents, "chrome.tabs.get");
  }
  std::string ExecuteScript(content::WebContents* contents) {
    return CallWithTabId(contents,
                         "((id) => chrome.scripting.executeScript("
                         "{target: {tabId: id}, func: () => 1}))");
  }

  extensions::TestExtensionDir dir_;
  std::string extension_id_;
};

}  // namespace

IN_PROC_BROWSER_TEST_F(KoanSpaceExtensionsBrowserTest, OffInAProfileOnlyThere) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const tab_groups::TabGroupId plain_space = spaces->GetSpaces()[0].id;
  ASSERT_NO_FATAL_FAILURE(LoadTestExtension());
  const std::string bank = AddProfile(u"Bank");
  const std::string work = AddProfile(u"Work");
  SetOff(bank, true);
  // Stored with the profile, in koan.containers.user.
  EXPECT_TRUE(containers::IsExtensionOffIn(
      profile()->GetPrefs()->GetList(prefs::kUserContainers),
      *containers::SpaceContainerFromString(bank), extension_id_));

  // A Space without a profile (the default partition): the extension works.
  content::WebContents* plain = OpenInActiveSpace("/title1.html?plain");
  ASSERT_TRUE(plain);
  EXPECT_TRUE(Injected(plain));
  EXPECT_TRUE(base::test::RunUntil([&] { return Saw("?plain"); }));
  // (Its rules load after the install: wait for the first block.)
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return FetchBlockedUrl(plain) == "blocked"; }));
  EXPECT_EQ("ok", TabsGet(plain));
  EXPECT_EQ("ok", ExecuteScript(plain));

  // Bank: off. Nothing injected, no request seen or blocked, no tab id.
  spaces->CreateSpace(u"Bank", u"", {}, bank);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  content::WebContents* in_bank = OpenInActiveSpace("/title1.html?bank");
  ASSERT_TRUE(in_bank);
  ASSERT_EQ(containers::SpaceContainerFromString(bank),
            containers::ContainerOf(in_bank));
  EXPECT_FALSE(Injected(in_bank));
  EXPECT_EQ("loaded", FetchBlockedUrl(in_bank));
  EXPECT_EQ("error", TabsGet(in_bank));
  EXPECT_EQ("error", ExecuteScript(in_bank));

  // Work: another profile, where it is on.
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  content::WebContents* in_work = OpenInActiveSpace("/title1.html?work");
  ASSERT_TRUE(in_work);
  EXPECT_TRUE(Injected(in_work));
  EXPECT_TRUE(base::test::RunUntil([&] { return Saw("?work"); }));
  EXPECT_EQ("blocked", FetchBlockedUrl(in_work));
  EXPECT_EQ("ok", TabsGet(in_work));
  // Bank's request was before Work's, and never reached the listener.
  EXPECT_FALSE(Saw("?bank"));

  // A Bank tab moved to the plain Space keeps Bank's profile, and its list.
  spaces->MoveTabsToSpace({tabs::TabInterface::GetFromContents(in_bank)},
                          plain_space);
  EXPECT_EQ("error", TabsGet(in_bank));
  EXPECT_EQ("loaded", FetchBlockedUrl(in_bank));
}

// The review's cases (PR #515): page loads and iframes, the tab list, tab and
// webNavigation events, the active tab and the context menu, in a tab of the
// profile the extension is off in and in one of another profile.
IN_PROC_BROWSER_TEST_F(KoanSpaceExtensionsBrowserTest,
                       NavigationsTabsAndMenusOnlyOutsideTheProfile) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_NO_FATAL_FAILURE(LoadTestExtension());
  const std::string bank = AddProfile(u"Bank");
  const std::string work = AddProfile(u"Work");
  SetOff(bank, true);

  // Bank, where it is off.
  spaces->CreateSpace(u"Bank", u"", {}, bank);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  content::WebContents* in_bank = OpenInActiveSpace("/title1.html?bank-new");
  ASSERT_TRUE(in_bank);
  ASSERT_EQ(containers::SpaceContainerFromString(bank),
            containers::ContainerOf(in_bank));
  // A page load and an iframe that the rule would block, and two it wouldn't.
  EXPECT_TRUE(PageLoads(in_bank, "/title2.html?koan-blocked-bank"));
  EXPECT_EQ("loaded",
            IframeLoads(in_bank, "/title2.html?koan-blocked-bank-sub"));
  EXPECT_TRUE(PageLoads(in_bank, "/title1.html?bank-main"));
  EXPECT_EQ("loaded", IframeLoads(in_bank, "/title2.html?bank-sub"));
  EXPECT_FALSE(Listed(in_bank));
  EXPECT_EQ("error", ActiveTabCall());
  EXPECT_FALSE(HasMenuItem(in_bank));

  // Work, where it is on: the same, the other way round.
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  content::WebContents* in_work = OpenInActiveSpace("/title1.html?work-new");
  ASSERT_TRUE(in_work);
  // (Its rules load after the install: wait for the first block.)
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !PageLoads(in_work, "/title2.html?koan-blocked-work"); }));
  EXPECT_EQ("blocked", IframeLoads(in_work, "/title2.html?koan-blocked-sub"));
  EXPECT_TRUE(PageLoads(in_work, "/title1.html?work-main"));
  EXPECT_EQ("loaded", IframeLoads(in_work, "/title2.html?work-sub"));
  // (Only the navigations themselves have these URLs.)
  EXPECT_TRUE(base::test::RunUntil([&] { return Has("seen", "?work-main"); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return Has("seen", "?work-sub"); }));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Has("committed", "?work-main"); }));
  EXPECT_TRUE(Has("committed", "?work-sub"));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Has("updated", "?work-main"); }));
  EXPECT_TRUE(Listed(in_work));
  EXPECT_EQ("ok", ActiveTabCall());
  EXPECT_TRUE(base::test::RunUntil([&] { return HasMenuItem(in_work); }));

  // Bank's loads and events came first, and none reached the extension.
  for (const char* marker :
       {"bank-new", "bank-main", "bank-sub", "koan-blocked-bank"}) {
    SCOPED_TRACE(marker);
    EXPECT_FALSE(Has("seen", marker));
    EXPECT_FALSE(Has("committed", marker));
    EXPECT_FALSE(Has("updated", marker));
  }
  // And Bank's tab is still hidden while another is active.
  EXPECT_FALSE(Listed(in_bank));
  EXPECT_FALSE(HasMenuItem(in_bank));
}

// Done applies a change to the profile's open tabs: they reload in new
// renderer processes, which load the extensions anew.
IN_PROC_BROWSER_TEST_F(KoanSpaceExtensionsBrowserTest,
                       ChangeRestartsTheProfilesTabs) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_NO_FATAL_FAILURE(LoadTestExtension());
  const std::string bank = AddProfile(u"Bank");
  const containers::ContainerId bank_id =
      *containers::SpaceContainerFromString(bank);
  content::WebContents* plain = OpenInActiveSpace("/title1.html?plain");
  ASSERT_TRUE(plain);
  spaces->CreateSpace(u"Bank", u"", {}, bank);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  content::WebContents* in_bank = OpenInActiveSpace("/title1.html?bank");
  ASSERT_TRUE(in_bank);
  EXPECT_TRUE(Injected(in_bank));

  // Off: the tab's process ends and the tab reloads in a new one, without
  // the extension.
  const int plain_process =
      plain->GetPrimaryMainFrame()->GetProcess()->GetDeprecatedID();
  SetOff(bank, true);
  RestartProfileTabs(profile(), bank_id);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !in_bank->IsCrashed() && !in_bank->IsLoading() &&
           in_bank->GetPrimaryMainFrame()->IsRenderFrameLive() &&
           content::EvalJs(in_bank,
                           "performance.getEntriesByType('navigation')[0].type")
                   .ExtractString() == "reload";
  }));
  EXPECT_EQ("/title1.html?bank",
            in_bank->GetLastCommittedURL().PathForRequest());
  EXPECT_FALSE(Injected(in_bank));
  // Other profiles' tabs are left alone.
  EXPECT_EQ(plain_process,
            plain->GetPrimaryMainFrame()->GetProcess()->GetDeprecatedID());
  EXPECT_TRUE(Injected(plain));

  // On again: the next restart brings the content script back.
  SetOff(bank, false);
  RestartProfileTabs(profile(), bank_id);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return !in_bank->IsCrashed() && !in_bank->IsLoading() &&
           in_bank->GetPrimaryMainFrame()->IsRenderFrameLive() &&
           Injected(in_bank);
  }));
}

}  // namespace koan
