// Does what the asking page's message says, for its tab, then answers true
// (the answer may not arrive when the page goes with its window). A message
// is an action's name, or {discard: [urls]}: unload the tabs showing those
// URLs, as Koan's Unload Space does (for the steps whose menu is a native
// NSMenu on macOS).
const ACTIONS = {
  'close-window': tab => chrome.windows.remove(tab.windowId),
  'pin-tab': tab => chrome.tabs.update(tab.id, { pinned: true }),
};
async function discard(urls) {
  const tabs = await chrome.tabs.query({});
  await Promise.all(tabs.filter(t => urls.includes(t.url) && !t.discarded && !t.active)
      .map(t => chrome.tabs.discard(t.id)));
}
chrome.runtime.onMessageExternal.addListener((message, sender, respond) => {
  const action = Array.isArray(message?.discard) ? () => discard(message.discard)
                                                 : ACTIONS[message];
  if (!action || !sender.tab) {
    respond(false);
    return;
  }
  action(sender.tab).then(() => respond(true), () => respond(false));
  return true;
});
