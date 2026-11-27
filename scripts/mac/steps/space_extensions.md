# Steps: extensions per Space profile (#473)

For the Mac loop and the hands-on tester, on macOS and on Linux (the Flatpak). Use a fresh profile past the welcome.
The design note is `.agent/notes/upstream/space-extensions.md`; the browser tests
(`KoanSpaceExtensionsBrowserTest.*`) cover the hooks, these steps the native menus and dialogs, which UI DevTools
can't click.

## The test extension

Make a folder `koan-473-ext` in Downloads (the Flatpak can read it) with these five files.

`manifest.json`:

```json
{
  "name": "Koan 473 test",
  "version": "1",
  "manifest_version": 3,
  "permissions": ["webRequest", "declarativeNetRequest", "scripting", "tabs", "sidePanel"],
  "host_permissions": ["<all_urls>"],
  "background": {"service_worker": "background.js"},
  "action": {"default_title": "Koan 473 test"},
  "side_panel": {"default_path": "panel.html"},
  "content_scripts": [{"matches": ["<all_urls>"], "js": ["content.js"], "run_at": "document_end"}],
  "declarative_net_request": {
    "rule_resources": [{"id": "rules", "enabled": true, "path": "rules.json"}]
  }
}
```

`content.js`:

```js
const bar = document.createElement('div');
bar.textContent = 'KOAN 473 TEST EXTENSION RAN HERE';
bar.style.cssText = 'position:fixed;top:0;left:0;right:0;z-index:2147483647;' +
    'background:#c00;color:#fff;font:16px sans-serif;padding:6px;text-align:center';
document.documentElement.append(bar);
```

`background.js`:

```js
self.seen = [];
chrome.webRequest.onBeforeRequest.addListener(
    (details) => { self.seen.push(details.url); }, {urls: ['<all_urls>']});
```

`rules.json`:

```json
[{"id": 1, "priority": 1, "action": {"type": "block"},
  "condition": {"urlFilter": "koan-blocked", "resourceTypes": ["xmlhttprequest"]}}]
```

`panel.html`: `<p>Koan 473 side panel</p>`

Load it: chrome://extensions > Developer mode > Load unpacked > `Downloads/koan-473-ext`. Pin it (the extensions
menu's pin).

**Checks used below.** *Bar*: the red bar is at the top of the page. *Block*: in the page's DevTools console,
`fetch('/?koan-blocked').then(() => 'loaded', () => 'blocked')` gives `"blocked"` when the extension's rule applies,
`"loaded"` when it doesn't. *Seen*: in chrome://extensions > Koan 473 test > "service worker" (its DevTools console),
`self.seen.filter(u => u.includes('<marker>')).length` is more than 0 when it saw the page's requests. *Tab id*: in
the same console, `(await chrome.tabs.query({})).map(t => [t.id, t.url])`, then for the tab's id
`await chrome.tabs.get(ID)` and `await chrome.scripting.executeScript({target: {tabId: ID}, func: () => document.title})`
work or throw "No tab with id".

## Steps

| # | Do | Expect |
|---|---|---|
| SE1 | Right-click the Space header. | After Set Profile ▸ there is Extensions…. |
| SE2 | Extensions… in the first Space (no profile). | "Extensions in This Space": the text says the Space uses the Default profile and that turning one off gives it a profile of its own. Koan 473 test is listed with its switch on. Only Done. |
| SE3 | Turn its switch off. | The dialog closes and New Profile asks for a name. Type "Bank", Create. Extensions… opens again, titled "Extensions in Bank", with the switch off. Set Profile ▸ now checks Bank. Press Done. |
| SE4 | In Bank, ⌘T (Ctrl+T on Linux), open `https://example.com/?bank`. | No red bar. Block gives `"loaded"`. Seen for `?bank` is 0. Tab id: both calls throw "No tab with id". The pinned extension's button is not in the URL pill (hover the sidebar). |
| SE5 | Create a Space without a profile (+ > Create Space, Profile Default), open `https://example.com/?plain`. | Red bar. Block gives `"blocked"`. Seen for `?plain` > 0. Both tab-id calls work. The button is in the pill. |
| SE6 | Switch back to Bank (its tab active). | The button leaves the pill again. |
| SE7 | Open the side panel of the extension from a plain tab (right-click the pill button > Open side panel), then switch to the Bank tab. | The side panel closes. Opening it from the extensions menu while the Bank tab is active closes it again at once. |
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
