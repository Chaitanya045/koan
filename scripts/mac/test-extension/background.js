// Audit tests 25 and 26 (.agent/notes/vertical-tabs-tests.md). Load unpacked
// (chrome://extensions, or --load-extension=<this dir>). Click the toolbar
// button (or send "run" from the service worker's DevTools) to run test 25;
// the results are in chrome.storage.local ("result") and in the service
// worker console. Test 26: press Command+Shift+L and see which one runs; the
// command's handler counts presses in chrome.storage.local ("shiftL").
async function run() {
  const result = {};
  const normal = await chrome.windows.create({type: 'normal', url: 'https://example.com/?ext-normal'});
  result.normal = normal.type;
  const popup = await chrome.windows.create({type: 'popup', url: 'https://example.com/?ext-popup'});
  result.popup = popup.type;
  // A group over the normal window's tab, collapsed and then ungrouped: the
  // Spaces' groups must keep their state (run it in a window with Spaces).
  const tab = normal.tabs[0];
  const groupId = await chrome.tabs.group({tabIds: [tab.id]});
  await chrome.tabGroups.update(groupId, {collapsed: true});
  result.groupCollapsed = (await chrome.tabGroups.get(groupId)).collapsed;
  await chrome.tabs.ungroup([tab.id]);
  await chrome.storage.local.set({result});
  console.log('koan-vtabs-ext', JSON.stringify(result));
}
chrome.action.onClicked.addListener(run);
chrome.runtime.onMessage.addListener((m) => { if (m === 'run') run(); });
chrome.commands.onCommand.addListener(async (command) => {
  if (command !== 'ext-shift-l') return;
  const {shiftL = 0} = await chrome.storage.local.get('shiftL');
  await chrome.storage.local.set({shiftL: shiftL + 1});
});
