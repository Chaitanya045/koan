# Steps: extensions per Space profile (#473, #544)

For the Mac loop and the hands-on tester, on macOS and on Linux (the Flatpak). Use a fresh profile past the welcome.
The design note is `.agent/notes/upstream/space-extensions.md`; the browser tests
(`KoanSpaceExtensionsBrowserTest.*`) cover the hooks, these steps the native menus and dialogs, which UI DevTools
can't click.

## The test extension

Make a folder `koan-473-ext` in Downloads (the Flatpak can read it) with these six files. (The PR's
video used the same files, with a `key` to fix the id.)

`manifest.json`:

```json
{
  "name": "Koan 473 test",
  "version": "1",
  "manifest_version": 3,
  "permissions": [
    "webRequest",
    "declarativeNetRequest",
    "scripting",
    "tabs",
    "webNavigation",
    "contextMenus",
    "sidePanel",
    "debugger",
    "activeTab",
    "history",
    "sessions",
    "topSites",
    "downloads"
  ],
  "host_permissions": [
    "<all_urls>"
  ],
  "background": {
    "service_worker": "background.js"
  },
  "action": {
    "default_title": "Koan 473 test"
  },
  "side_panel": {
    "default_path": "panel.html"
  },
  "commands": {
    "_execute_action": {
      "suggested_key": {
        "default": "Alt+Shift+K"
      }
    },
    "koan-cmd": {
      "suggested_key": {
        "default": "Alt+Shift+J"
      },
      "description": "Koan 473 command"
    }
  },
  "content_scripts": [
    {
      "matches": [
        "<all_urls>"
      ],
      "js": [
        "content.js"
      ],
      "run_at": "document_end",
      "all_frames": true
    }
  ],
  "declarative_net_request": {
    "rule_resources": [
      {
        "id": "rules",
        "enabled": true,
        "path": "rules.json"
      }
    ]
  }
}
```

`content.js` (a red bar in the page, and a mark in every frame):

```js
document.documentElement.dataset.koan473 = 'ran';
if (window === top) {
  const bar = document.createElement('div');
  bar.textContent = 'KOAN 473 TEST EXTENSION RAN HERE';
  bar.style.cssText = 'position:fixed;top:0;left:0;right:0;z-index:2147483647;' +
      'background:#c00;color:#fff;font:16px sans-serif;padding:6px;text-align:center';
  document.documentElement.append(bar);
}
```

`background.js` (everything the extension learns, in `self.log`):

```js
// Everything the extension learns about pages and tabs, in self.log.
self.log = [];
const add = (kind, d) => self.log.push(Object.assign({kind, t: Date.now()}, d));
chrome.webRequest.onBeforeRequest.addListener(
    d => add('webRequest', {type: d.type, url: d.url, tabId: d.tabId}), {urls: ['<all_urls>']});
chrome.webRequest.onBeforeSendHeaders.addListener(
    d => add('headers', {type: d.type, url: d.url,
                         cookie: (d.requestHeaders || []).find(h => h.name.toLowerCase() === 'cookie')?.value || ''}),
    {urls: ['<all_urls>']}, ['requestHeaders', 'extraHeaders']);
for (const name of ['onBeforeNavigate', 'onCommitted', 'onCompleted']) {
  chrome.webNavigation[name].addListener(
      d => add('webNavigation.' + name, {url: d.url, tabId: d.tabId, frameId: d.frameId}));
}
chrome.tabs.onUpdated.addListener((id, info, tab) => add('tabs.onUpdated', {tabId: id, url: tab.url || '', title: tab.title || ''}));
chrome.tabs.onCreated.addListener(tab => add('tabs.onCreated', {tabId: tab.id, url: tab.url || tab.pendingUrl || ''}));
chrome.tabs.onActivated.addListener(info => add('tabs.onActivated', {tabId: info.tabId}));
for (const name of ['onRemoved', 'onMoved', 'onAttached', 'onDetached', 'onZoomChange']) {
  chrome.tabs[name].addListener((a, b) => add('tabs.' + name, {tabId: a.tabId ?? a}));
}
chrome.history.onVisited.addListener(item => add('history.onVisited', {url: item.url}));
chrome.history.onVisitRemoved.addListener(r => add('history.onVisitRemoved', {urls: (r.urls || []).join(' ')}));
chrome.downloads.onCreated.addListener(item => add('downloads.onCreated', {id: item.id, url: item.url}));
chrome.downloads.onChanged.addListener(delta => add('downloads.onChanged', {id: delta.id}));
chrome.contextMenus.onClicked.addListener((info, tab) => add('contextMenus.onClicked', {url: info.pageUrl, tabUrl: tab?.url || ''}));
chrome.commands.onCommand.addListener((command, tab) => add('commands.onCommand', {command, url: tab?.url || ''}));
chrome.action.onClicked.addListener(tab => add('action.onClicked', {url: tab?.url || ''}));
chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({id: 'koan-473', title: 'Koan 473 item', contexts: ['all']});
});
chrome.sidePanel.setPanelBehavior({openPanelOnActionClick: true});
// What a call on a tab gives: 'ok: ...' or 'error: ...'.
self.tryCall = async (fn) => { try { const r = await fn(); return 'ok: ' + JSON.stringify(r)?.slice(0, 80); } catch (e) { return 'error: ' + e.message; } };
```

`rules.json`:

```json
[{"id": 1, "priority": 1, "action": {"type": "block"},
  "condition": {"urlFilter": "koan-blocked",
                "resourceTypes": ["xmlhttprequest", "main_frame", "sub_frame"]}}]
```

`panel.html`: `<p>Koan 473 side panel</p>`

Load it: chrome://extensions > Developer mode > Load unpacked > `Downloads/koan-473-ext`. Pin it (the extensions
menu's pin). Its shortcuts are Alt+Shift+K (its action: opens its side panel) and Alt+Shift+J (a named command);
check them at chrome://extensions/shortcuts.

**Checks used below.** *Bar*: the red bar is at the top of the page. *Block*: in the page's DevTools console,
`fetch('/?koan-blocked').then(() => 'loaded', () => 'blocked')` gives `"blocked"` when the extension's rule applies,
`"loaded"` when it doesn't. *Log*: in chrome://extensions > Koan 473 test > "service worker" (its DevTools console;
keep it open, so the worker stays alive), `self.log.filter(e => JSON.stringify(e).includes('<marker>'))` lists what
it learnt about URLs with the marker: `webRequest` (with `type`: `main_frame`, `sub_frame`, `xmlhttprequest`...),
`headers` (the `Cookie` it got with `extraHeaders`), `webNavigation.*`, `tabs.onUpdated`, `tabs.onCreated`,
`contextMenus.onClicked`, `commands.onCommand`. *Tab id*: in the same console,
`(await chrome.tabs.query({})).map(t => [t.id, t.url])`, then for the tab's id (its id from a tab outside the
profile's list doesn't work: take it from chrome://inspect or from a `tabs.onActivated` entry in the log)
`await tryCall(() => chrome.tabs.get(ID))` and
`await tryCall(() => chrome.scripting.executeScript({target: {tabId: ID}, func: () => document.title}))`
give `ok: ...` or `error: No tab with id`.

## Steps

| # | Do | Expect |
|---|---|---|
| SE1 | Right-click the Space header. | After Set Profile ▸ there is Extensions…. |
| SE2 | Extensions… in the first Space (no profile). | "Extensions in This Space": the text says the Space uses the Default profile and that turning one off gives it a profile of its own. Koan 473 test is listed with its switch on. Only Done. |
| SE3 | Turn its switch off. | The dialog closes and New Profile asks for a name. Type "Bank", Create. Extensions… opens again, titled "Extensions in Bank", with the switch off. Set Profile ▸ now checks Bank. Press Done. |
| SE4 | In Bank, ⌘T (Ctrl+T on Linux), open `https://example.com/?bank`. | No red bar. Block gives `"loaded"`. Seen for `?bank` is 0. Tab id: both calls throw "No tab with id". The pinned extension's button is not in the URL pill (hover the sidebar). |
| SE5 | Create a Space without a profile (+ > Create Space, Profile Default), open `https://example.com/?plain`. | Red bar. Block gives `"blocked"`. Seen for `?plain` > 0. Both tab-id calls work. The button is in the pill. |
| SE6 | Switch back to Bank (its tab active). | The button leaves the pill again. |
| SE7 | Open the side panel of the extension from a plain tab (Alt+Shift+K, or click its pill button), then switch to the Bank tab. | The side panel closes. Alt+Shift+K, or clicking the extension in the extensions menu, while the Bank tab is active does nothing. |
| SE8 | In Bank's tab, run `await chrome.tabs.captureVisibleTab()` in the extension's console (with the Bank window in front). | It throws "The extension is turned off in this tab's profile." From a plain tab it gives a data: URL. |
| SE9 | Right-click the URL pill's Extensions button (the last button, hover the sidebar to see it) in Bank. | A menu with "Extensions in This Space…", which opens the same dialog for Bank. A plain click still opens Chromium's extensions menu. |
| SE10 | In that dialog, turn the switch on and press Done. | Bank's tabs flash (they reload in new processes) and the red bar is now on Bank's page; Block gives `"blocked"`. Tabs in other Spaces don't reload. |
| SE11 | Turn it off again, Done. | Bank's tabs reload, without the bar. |
| SE12 | Drag the Bank tab to the plain Space (or Move To). | It keeps Bank's colour line and stays without the bar after a reload (the profile's list follows the tab). |
| SE13 | Create a Space with profile Bank too. | Its tabs have no bar either (the list is the profile's). |
| SE14 | Quit and start again. | Bank's tabs still have no bar; Extensions… for Bank still shows the switch off. |
| SE15 | Open a second window. | Its Bank Space has the same list. |
| SE16 | Layout: Sidebar and Top Toolbar (multiple toolbars), the extension pinned. | Its toolbar button hides while a Bank tab is active and shows in the plain Space. |
| SE17 | chrome://flags/#koan-space-extensions > Disabled, relaunch. | No Extensions… in the Space menu, no context menu on the pill button, and the bar is on Bank's pages (the list is kept but ignored). Set it back to Default. |
| SE18 | An Incognito window. | No Spaces, no Extensions…. |
| SE19 | Page loads (review finding 1). In Bank open `https://example.com/?koan-blocked-main`; in the plain Space the same URL. | Bank: the page loads, and Log for `koan-blocked-main` is empty. Plain: "This page has been blocked by an extension" (ERR_BLOCKED_BY_CLIENT), and Log has its `webRequest` `main_frame` entry. |
| SE20 | Iframes. In the page's console of a Bank tab and then a plain tab on `https://example.com/`: `f = document.createElement('iframe'); f.src = 'https://example.org/?koan-blocked-sub'; document.body.append(f)`, then the same with `?sub-ok`. | Bank: both frames load, Log for `koan-blocked-sub` and `sub-ok` is empty, no bar marker in the frames. Plain: the first frame is blocked, the second loads; Log has `sub_frame` entries and `webNavigation.onCommitted` with `frameId` > 0. |
| SE21 | Cookies with `extraHeaders`. Sign in somewhere in Bank (or run `document.cookie = 'k=bank'` in a Bank page and reload). | Log has no `headers` entry with Bank's URL or cookie. In a plain tab the same gives `headers` entries with its `Cookie`. |
| SE22 | Tabs and their events (finding 2). With a Bank tab and a plain tab open, run `(await chrome.tabs.query({})).map(t => t.url)` and `(await chrome.windows.getAll({populate: true})).flatMap(w => w.tabs.map(t => t.url))`; load a new page in Bank. | Neither lists Bank's tab. Log has no `tabs.onUpdated`, `tabs.onCreated` or `webNavigation.*` entry for Bank's URLs. It has no `tabs.onActivated` entry when you switch to Bank (#544: the events with ids only aren't sent either). In the plain Space all of them are there, with URLs and titles. |
| SE23 | The active tab. With Bank's tab active: `await tryCall(() => chrome.tabs.getZoom())`, `await tryCall(() => chrome.tabs.update({url: 'https://example.org/'}))`, `await tryCall(() => chrome.tabs.captureVisibleTab())`. | All three `error:` ("No selected tab", the last "turned off in this tab's profile"), and Bank's tab doesn't navigate. With a plain tab active all three work. |
| SE24 | The debugger. `await tryCall(() => chrome.debugger.attach({tabId: ID}, '1.3'))` with Bank's tab id, and `(await chrome.debugger.getTargets()).map(t => t.url)`. | Attach: `error:`. getTargets doesn't list Bank's pages. For a plain tab attach works (a "started debugging" bar shows; `chrome.debugger.detach({tabId: ID})`). |
| SE25 | Context menu (finding 3). Right-click a Bank page, then a plain page. | Bank: no "Koan 473 item". Plain: "Koan 473 item"; clicking it adds a `contextMenus.onClicked` entry with the page's URL. |
| SE26 | Shortcuts. In a Bank tab press Alt+Shift+J, then Alt+Shift+K; then the same in a plain tab. | Bank: nothing: no `commands.onCommand` in Log, no side panel. Plain: a `commands.onCommand` entry with the plain tab's URL, and the side panel opens. |

### The stores every profile shares (#544)

Bank has the extension off; Work is another profile where it is on. Visit `https://example.com/?bank-only` and
`https://example.org/?both` in a Bank tab, `https://example.net/?work-only` and `https://example.org/?both` in a
Work tab.

| # | Do | Expect |
|---|---|---|
| SE27 | History. In the extension's console: `(await chrome.history.search({text: '', startTime: 0})).map(h => [h.url, h.visitCount])`, `(await chrome.history.getVisits({url: 'https://example.com/?bank-only'})).length`, `(await chrome.history.getVisits({url: 'https://example.org/?both'})).length`, and Log for `history.onVisited`. | No `bank-only`; `?both` with visit count 1 (Work's); 0 and 1 visits; Log has `history.onVisited` for `work-only` and one `?both`, none for `bank-only`. |
| SE28 | Forgetting a page. Library > History, Which profile? Bank, right-click `bank-only` > Forget This Page. | No `history.onVisitRemoved` entry naming `bank-only` in Log. Forget `work-only` in Work: an entry naming it. |
| SE29 | Top sites. `(await chrome.topSites.get()).map(s => s.url)`. | No `bank-only` (only Bank visited it); `work-only` and `?both` can be there. |
| SE30 | Downloads. Download a file in Bank (e.g. right-click a link > Save Link As..., or any download link), then one in Work; `(await chrome.downloads.search({})).map(d => [d.id, d.url])`, then `await chrome.downloads.search({id: ID})` and `await tryCall(() => chrome.downloads.show(ID))` with Bank's download's id (chrome://downloads has the order). | Only Work's download is listed; Bank's id gives `[]` and `error: Invalid downloadId`. Log has `downloads.onCreated`/`onChanged` for Work's only. After a restart the same (the partition is kept with the download). |
| SE31 | Closed tabs. Close a Bank tab and a Work tab, then `(await chrome.sessions.getRecentlyClosed()).map(s => s.tab?.url ?? s.window.tabs.map(t => t.url))` and `await tryCall(() => chrome.sessions.restore())`. | Work's tab only. Restore without an id, with Bank's tab closed last: `error: There are no recently closed sessions.` (it doesn't reopen Bank's tab). |
| SE32 | Tab events with ids only. Switch to a Bank tab and back, move it, zoom it (Cmd/Ctrl +), close it. | No `tabs.onActivated`, `onMoved`, `onZoomChange` or `onRemoved` with Bank's tab id in Log; the same in Work gives each. |
