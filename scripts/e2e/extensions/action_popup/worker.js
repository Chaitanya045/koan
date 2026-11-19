// The badge sidebar_top_row.mjs looks for on the pill's button.
chrome.action.setBadgeText({ text: '135' });
chrome.runtime.onStartup.addListener(() => chrome.action.setBadgeText({ text: '135' }));
