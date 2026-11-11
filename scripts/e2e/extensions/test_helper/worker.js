// Does what the asking page's message says, for its tab, then answers true
// (the answer may not arrive when the page goes with its window).
const ACTIONS = {
  'close-window': tab => chrome.windows.remove(tab.windowId),
  'pin-tab': tab => chrome.tabs.update(tab.id, { pinned: true }),
};
chrome.runtime.onMessageExternal.addListener((message, sender, respond) => {
  const action = ACTIONS[message];
  if (!action || !sender.tab) {
    respond(false);
    return;
  }
  action(sender.tab).then(() => respond(true), () => respond(false));
  return true;
});
