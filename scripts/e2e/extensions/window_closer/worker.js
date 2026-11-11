// Closes the asking page's window, then answers (the answer may not arrive:
// the page goes with its window).
chrome.runtime.onMessageExternal.addListener((message, sender, respond) => {
  if (message === 'close-window' && sender.tab) {
    chrome.windows.remove(sender.tab.windowId).then(() => respond(true), () => respond(false));
    return true;
  }
  respond(false);
});
