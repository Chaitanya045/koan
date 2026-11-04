// End-to-end regression test for closing a window (the crash in
// KoanCompactModeController::UpdateCaptionButtons() <- Deactivate() <-
// OnBrowserViewDestroying(), from the widget being gone while the BrowserView
// is destroyed). A second window is opened and closed in each state, and
// after each the browser must still answer and the first window be as it was:
// - plain (single toolbar, the default);
// - with compact mode on (Ctrl+S);
// - in native fullscreen (macOS: fullscreen is Chromium's immersive mode);
// - in compact mode and fullscreen.
//
//   scripts/e2e/run.sh window_close
import { CTRL, browserCall, check, finish, key, pages, sleep, views, visiblePage }
    from './lib/koan.mjs';
import { alive, goFullscreen, isMac } from './lib/fullscreen.mjs';

const VK = { S: 83 };
const stripWidths = async () => (await views())
    .filter(v => v.cls === 'VerticalTabStripRegionView').map(v => v.w);

let serial = 0;
async function openWindow() {
  const r = await browserCall('Target.createTarget',
      { url: `https://example.com/?close${++serial}`, newWindow: true });
  await sleep(2000);
  return r.result.targetId;
}
async function closeWindow(targetId) {
  await browserCall('Target.closeTarget', { targetId });
  await sleep(1500);
}
async function closeIn(when, { fullscreen }) {
  const id = await openWindow();
  if (fullscreen) {
    await goFullscreen(true, id);
  }
  await closeWindow(id);
  check(`${when}: closing the window doesn't crash`, await alive());
  check(`${when}: the first window is still there`, (await pages()).length >= 1);
}

const first = (await stripWidths())[0];
await closeIn('Plain', { fullscreen: false });
if (await isMac()) {
  await closeIn('Fullscreen', { fullscreen: true });
}

// Compact mode on in the first window (a profile pref: all windows).
await key(await visiblePage(), VK.S, CTRL, 's');
await sleep(900);
await closeIn('Compact mode', { fullscreen: false });
if (await isMac()) {
  await closeIn('Compact mode and fullscreen', { fullscreen: true });
}

// Back to the sidebar as it was.
await key(await visiblePage(), VK.S, CTRL, 's');
await sleep(1200);
check('The first window\'s sidebar is as it was',
      (await stripWidths())[0] === first, `${(await stripWidths())[0]} vs ${first}`);

finish();
