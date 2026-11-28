// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Extensions per Space profile (#473,
// .agent/notes/upstream/space-extensions.md): an extension that is off in a
// Space's profile injects nothing into its tabs, sees none of their requests
// (webRequest, declarativeNetRequest; subresources and navigations), doesn't
// see the tabs (tabs.query, tab and webNavigation events), can't address them
// (tab ids, the active tab) and has no context menu item in them, gets none
// of the profile's history, closed tabs, top sites and downloads (#544), and
// still works in every other Space. A change applies to the profile's open
// tabs by restarting them. Its side panel stays closed there, and the pages
// that use the tabs API with no extension (the bookmarks side panel,
// chrome://bookmarks) keep working in every Space (#602).

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/context_menu_matcher.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/history/top_sites_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_test_util.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/download/public/common/download_item.h"
#include "components/history/core/browser/top_sites.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/download_manager.h"
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
#include "koan/browser/containers/koan_history_profiles.h"
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
// requests it sees (type and URL), webNavigation.onCommitted,
// tabs.onUpdated, history.onVisited and downloads.onCreated listeners that keep
// the URLs they get, tabs.onActivated and onRemoved listeners that keep the tab
// ids, a context menu item, and a declarativeNetRequest rule that blocks
// "koan-blocked" (fetches, page loads, iframes).
constexpr char kManifest[] = R"({
  "name": "Koan Space extensions test",
  "version": "1",
  "manifest_version": 3,
  "permissions": ["webRequest", "declarativeNetRequest", "scripting", "tabs",
                  "webNavigation", "contextMenus", "history", "sessions",
                  "topSites", "downloads"],
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
  self.visited = [];
  self.downloaded = [];
  self.activated = [];
  self.removed = [];
  chrome.history.onVisited.addListener(
      (item) => { self.visited.push(item.url); });
  chrome.downloads.onCreated.addListener(
      (item) => { self.downloaded.push(item.url); });
  chrome.tabs.onActivated.addListener(
      (info) => { self.activated.push(info.tabId); });
  chrome.tabs.onRemoved.addListener((id) => { self.removed.push(id); });
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

// An extension with only a side panel (sidePanel.setOptions' default page).
constexpr char kSidePanelManifest[] = R"({
  "name": "Koan Space side panel test",
  "version": "1",
  "manifest_version": 3,
  "permissions": ["sidePanel"],
  "side_panel": {"default_path": "panel.html"}
})";

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

  void LoadSidePanelExtension() {
    dir_.WriteManifest(kSidePanelManifest);
    dir_.WriteFile(FILE_PATH_LITERAL("panel.html"), "<title>Panel</title>");
    scoped_refptr<const extensions::Extension> extension =
        extensions::ChromeTestExtensionLoader(profile()).LoadExtension(
            dir_.UnpackedPath());
    ASSERT_TRUE(extension);
    extension_id_ = extension->id();
  }

  // Opens `path` in a new tab of the active Space, as Cmd+T and the URL bar
  // do (its profile's partition).
  content::WebContents* OpenInActiveSpace(const std::string& path) {
    return OpenUrlInActiveSpace(embedded_test_server()->GetURL(path));
  }
  content::WebContents* OpenUrlInActiveSpace(const GURL& url) {
    NavigateParams params(browser(), url, ui::PAGE_TRANSITION_TYPED);
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

  // Whether `list` (self.activated, self.removed) has the tab's id.
  bool HasTabId(const std::string& list, int id) {
    return RunInBackground(
               base::StrCat({"chrome.test.sendScriptResult(self.", list,
                             ".includes(", base::NumberToString(id), "));"}))
        .GetIfBool()
        .value_or(false);
  }

  // The URLs `call` (a promise of a list of objects with `field`) gives, one
  // per line.
  std::string Urls(const std::string& call, const std::string& field) {
    base::Value result = RunInBackground(base::StrCat(
        {call, ".then((items) => chrome.test.sendScriptResult(items.map(",
         "(item) => item.", field, ").join('\\n')));"}));
    return result.is_string() ? result.GetString() : std::string();
  }

  // What history.search for every page gives, one "<url> <visitCount>" per
  // line.
  std::string HistorySearch() {
    base::Value result = RunInBackground(
        "chrome.history.search({text: '', startTime: 0, maxResults: 100})"
        ".then((items) => chrome.test.sendScriptResult(items.map("
        "(item) => item.url + ' ' + item.visitCount).join('\\n')));");
    return result.is_string() ? result.GetString() : std::string();
  }

  // How many visits history.getVisits gives for `url`.
  int VisitsOf(const GURL& url) {
    base::Value result = RunInBackground(
        base::StrCat({"chrome.history.getVisits({url: '", url.spec(),
                      "'}).then((visits) => "
                      "chrome.test.sendScriptResult(visits.length));"}));
    return result.GetIfInt().value_or(-1);
  }

  // Downloads `path` from `contents`'s page, as a link with a download
  // attribute does (in the tab's partition).
  void DownloadFrom(content::WebContents* contents, const std::string& path) {
    ASSERT_TRUE(content::ExecJs(
        contents, base::StrCat({"const a = document.createElement('a');"
                                "a.href = '",
                                path,
                                "'; a.download = 'koan.html';"
                                "document.body.appendChild(a); a.click();"})));
  }

  // Closes `contents` into the recently closed list, as Cmd+W does.
  void CloseTab(content::WebContents* contents) {
    TabStripModel* model = browser()->tab_strip_model();
    model->CloseWebContentsAt(model->GetIndexOfWebContents(contents),
                              TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
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

  // The bookmarks side panel's page, once it has loaded.
  content::WebContents* BookmarksPanelPage() {
    for (content::WebContents* contents : content::GetAllWebContents()) {
      if (contents->GetLastCommittedURL().host() ==
              chrome::kChromeUIBookmarksSidePanelHost &&
          !contents->IsLoading()) {
        return contents;
      }
    }
    return nullptr;
  }

  // The ids of the tabs that `page`, a WebUI page that may use the tabs API
  // (chrome://bookmarks, the bookmarks side panel), gets from
  // chrome.tabs.query({}). It calls the API with no extension.
  std::set<int> TabIdsFor(content::WebContents* page) {
    const content::EvalJsResult result =
        content::EvalJs(page,
                        "chrome.tabs.query({}).then("
                        "(tabs) => tabs.map((tab) => tab.id))");
    std::set<int> ids;
    if (result.is_list()) {
      for (const base::Value& id : result.ExtractList()) {
        if (id.is_int()) {
          ids.insert(id.GetInt());
        }
      }
    }
    return ids;
  }

  // Ctrl+B: the bookmarks side panel opens, its page asks for the tabs
  // (chrome.tabs.query), and it lists `tab`.
  void OpenBookmarksPanel(content::WebContents* tab) {
    SidePanelUI* side_panel = SidePanelUI::From(browser());
    ASSERT_TRUE(side_panel);
    chrome::ExecuteCommand(browser(), IDC_SHOW_BOOKMARK_SIDE_PANEL);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return side_panel->IsSidePanelEntryShowing(
          SidePanelEntryKey(SidePanelEntryId::kBookmarks));
    }));
    content::WebContents* page = nullptr;
    ASSERT_TRUE(base::test::RunUntil([&] {
      page = BookmarksPanelPage();
      return page != nullptr;
    }));
    EXPECT_TRUE(
        TabIdsFor(page).contains(extensions::ExtensionTabUtil::GetTabId(tab)));
    // Still open after the Space guard's check (posted when it showed).
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, loop.QuitClosure());
    loop.Run();
    EXPECT_TRUE(side_panel->IsSidePanelEntryShowing(
        SidePanelEntryKey(SidePanelEntryId::kBookmarks)));
    side_panel->Close();
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return !side_panel->IsSidePanelShowing(); }));
  }

  // What `call` (a tabs function without a tab id: the active tab) gives
  // `page`, a WebUI page with no extension: "ok" or its error.
  std::string ActiveTabCallFrom(content::WebContents* page,
                                const std::string& call) {
    const content::EvalJsResult result = content::EvalJs(
        page, base::StrCat({call, ".then(() => 'ok', (e) => e.message)"}));
    if (result.is_string()) {
      return result.ExtractString();
    }
    return result.is_ok() ? "not a string" : result.ExtractError();
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

// The stores every profile shares (#544): history, the recently closed tabs,
// top sites and downloads give the extension nothing of the profile it is off
// in, and id-only tab events about its tabs don't reach it; all of these
// still work for another profile's tabs.
IN_PROC_BROWSER_TEST_F(KoanSpaceExtensionsBrowserTest,
                       SharedStoresLeaveOutTheProfile) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  base::ScopedTempDir downloads;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
  }
  profile()->GetPrefs()->SetFilePath(::prefs::kDownloadDefaultDirectory,
                                     downloads.GetPath());
  profile()->GetPrefs()->SetBoolean(::prefs::kPromptForDownload, false);
  ASSERT_NO_FATAL_FAILURE(LoadTestExtension());
  const std::string bank = AddProfile(u"Bank");
  const std::string work = AddProfile(u"Work");
  SetOff(bank, true);
  const GURL both = embedded_test_server()->GetURL("/title1.html?both");

  // Bank, where it is off: a page only Bank visits, one both visit, and a
  // download.
  spaces->CreateSpace(u"Bank", u"", {}, bank);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  content::WebContents* in_bank = OpenInActiveSpace("/title1.html?bank-only");
  ASSERT_TRUE(in_bank);
  const int bank_tab = extensions::ExtensionTabUtil::GetTabId(in_bank);
  ASSERT_TRUE(content::NavigateToURL(in_bank, both));
  ASSERT_NO_FATAL_FAILURE(DownloadFrom(in_bank, "/title2.html?bank-download"));

  // Work, where it is on: the same.
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  content::WebContents* in_work = OpenInActiveSpace("/title1.html?work-only");
  ASSERT_TRUE(in_work);
  const int work_tab = extensions::ExtensionTabUtil::GetTabId(in_work);
  ASSERT_TRUE(content::NavigateToURL(in_work, both));
  ASSERT_NO_FATAL_FAILURE(DownloadFrom(in_work, "/title2.html?work-download"));

  // history.onVisited: only Work's visits.
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Has("visited", "work-only"); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return Has("visited", "?both"); }));
  EXPECT_FALSE(Has("visited", "bank-only"));
  EXPECT_EQ(1, RunInBackground("chrome.test.sendScriptResult(self.visited"
                               ".filter(u => u.includes('?both')).length);")
                   .GetIfInt()
                   .value_or(-1));

  // history.search and getVisits: the page both visited has Work's visit
  // only; Bank's page isn't there at all.
  const std::string found = HistorySearch();
  EXPECT_NE(std::string::npos, found.find("work-only")) << found;
  EXPECT_EQ(std::string::npos, found.find("bank-only")) << found;
  EXPECT_NE(std::string::npos, found.find(both.spec() + " 1")) << found;
  EXPECT_EQ(1, VisitsOf(both));
  EXPECT_EQ(0,
            VisitsOf(embedded_test_server()->GetURL("/title1.html?bank-only")));
  EXPECT_EQ(1,
            VisitsOf(embedded_test_server()->GetURL("/title1.html?work-only")));

  // downloads.search and onCreated: Work's download only, and Bank's can't be
  // looked up by its id.
  content::DownloadManager* manager = profile()->GetDownloadManager();
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::DownloadManager::DownloadVector items;
    manager->GetAllDownloads(&items);
    return items.size() == 2u;
  }));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Has("downloaded", "work-download"); }));
  EXPECT_FALSE(Has("downloaded", "bank-download"));
  const std::string listed = Urls("chrome.downloads.search({})", "url");
  EXPECT_NE(std::string::npos, listed.find("work-download")) << listed;
  EXPECT_EQ(std::string::npos, listed.find("bank-download")) << listed;
  {
    content::DownloadManager::DownloadVector items;
    manager->GetAllDownloads(&items);
    for (download::DownloadItem* item : items) {
      const bool in_bank_download =
          item->GetURL().spec().find("bank-download") != std::string::npos;
      SCOPED_TRACE(item->GetURL().spec());
      EXPECT_EQ(in_bank_download ? "" : item->GetURL().spec(),
                Urls(base::StrCat({"chrome.downloads.search({id: ",
                                   base::NumberToString(item->GetId()), "})"}),
                     "url"));
    }
  }

  // tabs.onActivated: switching to Bank's tab isn't announced, to Work's is.
  TabStripModel* model = browser()->tab_strip_model();
  model->ActivateTabAt(model->GetIndexOfWebContents(in_bank));
  model->ActivateTabAt(model->GetIndexOfWebContents(in_work));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return HasTabId("activated", work_tab); }));
  EXPECT_FALSE(HasTabId("activated", bank_tab));

  // topSites.get: Work's page, not Bank's.
  scoped_refptr<history::TopSites> top_sites =
      TopSitesFactory::GetForProfile(profile());
  ASSERT_TRUE(top_sites);
  top_sites->SyncWithHistory();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return Urls("chrome.topSites.get()", "url").find("work-only") !=
           std::string::npos;
  }));
  EXPECT_EQ(std::string::npos,
            Urls("chrome.topSites.get()", "url").find("bank-only"));

  // sessions.getRecentlyClosed and tabs.onRemoved: Work's closed tab only,
  // and Bank's can't be restored by its id.
  CloseTab(in_bank);
  CloseTab(in_work);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return HasTabId("removed", work_tab); }));
  EXPECT_FALSE(HasTabId("removed", bank_tab));
  sessions::TabRestoreService* restore =
      TabRestoreServiceFactory::GetForProfile(profile());
  ASSERT_TRUE(restore);
  std::optional<SessionID> bank_closed;
  for (const auto& entry : restore->entries()) {
    if (entry->type == sessions::tab_restore::Type::TAB &&
        entry->extra_data.contains(containers::kContainerSessionKey) &&
        entry->extra_data.at(containers::kContainerSessionKey) == bank) {
      bank_closed = entry->id;
    }
  }
  ASSERT_TRUE(bank_closed);
  const std::string closed =
      Urls("chrome.sessions.getRecentlyClosed()", "tab && item.tab.url");
  EXPECT_NE(std::string::npos, closed.find("?both")) << closed;
  EXPECT_EQ(std::string::npos, closed.find("bank")) << closed;
  base::Value restored = RunInBackground(base::StrCat(
      {"chrome.sessions.restore('", base::NumberToString(bank_closed->id()),
       "').then(() => chrome.test.sendScriptResult('ok'),"
       " () => chrome.test.sendScriptResult('error'));"}));
  EXPECT_EQ("error", restored.is_string() ? restored.GetString() : "");
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

// Ctrl+B in a new Space and in one whose profile has an extension off
// (#602): the bookmarks side panel opens and lists the Space's tab. Its page
// and chrome://bookmarks call the tabs API with no extension, which the
// hooks for an extension that is off let through.
IN_PROC_BROWSER_TEST_F(KoanSpaceExtensionsBrowserTest,
                       BookmarksSidePanelInEverySpace) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_NO_FATAL_FAILURE(LoadTestExtension());
  SidePanelUI::From(browser())->DisableAnimationsForTesting();
  const std::string bank = AddProfile(u"Bank");
  SetOff(bank, true);

  // A new Space without a profile.
  spaces->CreateSpace(u"Work", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  content::WebContents* in_work = OpenInActiveSpace("/title1.html?work");
  ASSERT_TRUE(in_work);
  ASSERT_NO_FATAL_FAILURE(OpenBookmarksPanel(in_work));

  // A Space whose profile has the extension off.
  spaces->CreateSpace(u"Bank", u"", {}, bank);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  content::WebContents* in_bank = OpenInActiveSpace("/title1.html?bank");
  ASSERT_TRUE(in_bank);
  ASSERT_EQ(containers::SpaceContainerFromString(bank),
            containers::ContainerOf(in_bank));
  ASSERT_NO_FATAL_FAILURE(OpenBookmarksPanel(in_bank));
  // The extension still doesn't see the tab.
  EXPECT_FALSE(Listed(in_bank));

  // chrome://bookmarks in that Space: the tabs functions that take the active
  // tab, and the tab list.
  content::WebContents* manager =
      OpenUrlInActiveSpace(GURL(chrome::kChromeUIBookmarksURL));
  ASSERT_TRUE(manager);
  ASSERT_TRUE(content::WaitForLoadStop(manager));
  EXPECT_EQ("ok", ActiveTabCallFrom(manager, "chrome.tabs.getZoom()"));
  EXPECT_EQ("ok",
            ActiveTabCallFrom(manager, "chrome.tabs.update({active: true})"));
  const std::set<int> ids = TabIdsFor(manager);
  EXPECT_TRUE(ids.contains(extensions::ExtensionTabUtil::GetTabId(in_bank)));
  EXPECT_TRUE(ids.contains(extensions::ExtensionTabUtil::GetTabId(in_work)));
}

// #515's side panel case: an extension's side panel closes when it would
// show while a tab of a profile the extension is off in is active (opened
// there, or that tab activated with it open), and shows in another profile.
// The bookmarks side panel stays open there.
IN_PROC_BROWSER_TEST_F(KoanSpaceExtensionsBrowserTest,
                       ExtensionSidePanelOnlyOutsideTheProfile) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ASSERT_NO_FATAL_FAILURE(LoadSidePanelExtension());
  SidePanelUI* side_panel = SidePanelUI::From(browser());
  ASSERT_TRUE(side_panel);
  side_panel->DisableAnimationsForTesting();
  const SidePanelEntryKey panel(SidePanelEntryId::kExtension, extension_id_);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return SidePanelRegistry::From(browser())->GetEntryForKey(panel) != nullptr;
  }));
  int shown = 0;
  base::CallbackListSubscription shown_subscription =
      side_panel->RegisterSidePanelShown(
          base::BindLambdaForTesting([&] { ++shown; }));
  const std::string bank = AddProfile(u"Bank");
  const std::string work = AddProfile(u"Work");
  SetOff(bank, true);

  // Work, where it is on: the panel shows.
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  content::WebContents* in_work = OpenInActiveSpace("/title1.html?work");
  ASSERT_TRUE(in_work);
  side_panel->Show(panel);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return side_panel->IsSidePanelEntryShowing(panel); }));

  // A tab of Bank, where it is off, becomes active: the panel closes.
  spaces->CreateSpace(u"Bank", u"", {}, bank);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  content::WebContents* in_bank = OpenInActiveSpace("/title1.html?bank");
  ASSERT_TRUE(in_bank);
  ASSERT_EQ(containers::SpaceContainerFromString(bank),
            containers::ContainerOf(in_bank));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !side_panel->IsSidePanelShowing(); }));

  // Opened in Bank: it shows and closes at once.
  const int shown_before = shown;
  side_panel->Show(panel);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return shown > shown_before && !side_panel->IsSidePanelShowing();
  }));

  // The bookmarks side panel stays open in Bank.
  ASSERT_NO_FATAL_FAILURE(OpenBookmarksPanel(in_bank));

  // Back in Work's tab, the panel shows again.
  TabStripModel* model = browser()->tab_strip_model();
  model->ActivateTabAt(model->GetIndexOfWebContents(in_work));
  side_panel->Show(panel);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return side_panel->IsSidePanelEntryShowing(panel); }));
}

}  // namespace koan
