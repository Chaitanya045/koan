// Does what the asking page's message says, for its tab, then answers (the
// answer may not arrive when the page goes with its window). A message is an
// action's name, or {discard: [urls]}: unload the tabs showing those URLs, as
// Koan's Unload Space does (for the steps whose menu is a native NSMenu on
// macOS), or {close: [tab ids]}: close those tabs, or {activate: url}: show
// the tab showing that URL (an unloaded one loads). Actions answer true, except
// 'list-tabs', which answers the asking window's tabs in order (for
// scripts/mac/video/flows: what the model has, next to what the sidebar
// draws).
const ACTIONS = {
  'close-window': tab => chrome.windows.remove(tab.windowId),
  'pin-tab': tab => chrome.tabs.update(tab.id, { pinned: true }),
  'unpin-tab': tab => chrome.tabs.update(tab.id, { pinned: false }),
  'list-tabs': async tab => (await chrome.tabs.query({ windowId: tab.windowId })).map(t => ({
    id: t.id, index: t.index, url: t.url || t.pendingUrl || '', title: t.title ?? '',
    pinned: t.pinned, groupId: t.groupId, active: t.active, status: t.status,
    discarded: t.discarded, audible: !!t.audible, asker: t.id === tab.id })),
};
async function discard(urls) {
  const tabs = await chrome.tabs.query({});
  await Promise.all(tabs.filter(t => urls.includes(t.url) && !t.discarded && !t.active)
      .map(t => chrome.tabs.discard(t.id)));
}
async function activate(url) {
  const [tab] = (await chrome.tabs.query({})).filter(t => (t.url || t.pendingUrl) === url);
  if (tab) await chrome.tabs.update(tab.id, { active: true });
  return !!tab;
}
chrome.runtime.onMessageExternal.addListener((message, sender, respond) => {
  const action = Array.isArray(message?.discard) ? () => discard(message.discard)
               : Array.isArray(message?.close) ? () => chrome.tabs.remove(message.close)
               : typeof message?.activate === 'string' ? () => activate(message.activate)
                                               : ACTIONS[message];
  if (!action || !sender.tab) {
    respond(false);
    return;
  }
  action(sender.tab).then(r => respond(r ?? true), () => respond(false));
  return true;
});
