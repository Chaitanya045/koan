// End-to-end test of a window's teardown order (#193, #153): a second window
// closed in each layout, a second window's last tab closed, and a quit with
// two windows. Pass: Koan keeps running (until the quit), nothing fatal in
// its log, and every closed window logs "Koan: window torn down" without
// "(widget gone)" (KoanBrowserViewController::TearDown() ran before the
// widget went; window_teardown.flags turns its VLOG on).
//
// The windows open through CDP (Target.createTarget), a navigation in a new
// window: prefs/window_teardown.json turns off Zen's open-link-in-new-
// unsynced-window, so they are normal windows (Spaces, window sync), not New
// Blank Windows.
//
// Layouts: single toolbar (the default), compact mode, native fullscreen,
// compact + fullscreen, tabs on the right, multiple toolbars. On macOS each
// also checks that File > Close Window is enabled (System Events), and the
// quit that Quit is. The window closes with the Close
// Window shortcut (Cmd+Shift+W on macOS; the red button runs the same
// BrowserView::Close path) through the real keyboard when suite.sh allows
// real input, after making that window key (makeKey(): a window opened
// through CDP isn't). Else, on macOS, through the test_helper extension
// (window_teardown.flags; synthetic keys don't reach the menu that owns the
// key), and through CDP elsewhere; the quit, through CDP's Browser.close.
// Run by scripts/mac/suite.sh (or scripts/e2e/run.sh window_teardown off
// macOS, without fullscreen).
import { browserCall, check, finish, onPage, pages, setKoanPref, sleep,
         waitFor } from './lib/koan.mjs';
import { ACCEL, CLOSE_WINDOW_ITEM, IS_MAC, QUIT_ITEM, SHIFT, alive, closeWindowOf, countInLog,
         fatalInLog, key, makeKey, menuItemEnabled,
         openWindow, realInput, realKey, setWindowState, startServer, windowOf,
         windows } from './lib/mac.mjs';

const server = await startServer();
const TORN = /Koan: window torn down$/m;
const GONE = /Koan: window torn down \(widget gone\)/;
const torn = () => countInLog(TORN);
const gone = () => countInLog(GONE);

check('The browser\'s log is readable (KOAN_LOG)', !!process.env.KOAN_LOG,
      process.env.KOAN_LOG ?? 'no KOAN_LOG');

// Whether the menu bar's `item` is enabled, checked as `name` where
// Accessibility can read it (macOS, with the window key).
function checkMenuItem(name, item) {
  const enabled = menuItemEnabled(item);
  if (enabled !== null) check(name, enabled);
  else if (IS_MAC) console.log(`SKIP  ${name}: System Events can't read the menu bar`);
}

// Closes window `windowId` with the Close Window shortcut, typed into its
// active page (or closeWindowOf() it); waits until it is gone. On macOS File >
// Close Window must be enabled for it first (Koan's lights once disabled it).
async function closeWindow(windowId, name = 'The window') {
  const page = (await windows()).find(w => w.windowId === windowId)?.pages
      .find(p => !p.url.startsWith('devtools:'));
  if (IS_MAC) {
    if (!(await makeKey(page))) console.log(`  (window ${windowId}'s page never got the focus)`);
    checkMenuItem(`${name}: File > Close Window is enabled`, CLOSE_WINDOW_ITEM);
  }
  if (realInput()) {
    // The real key goes to the key window: make it this one (a window opened
    // through CDP isn't key).
    if (!(await makeKey(page))) console.log(`  (window ${windowId}'s page never got the focus)`);
    await realKey('w', 'cs', 800);
  } else {
    await closeWindowOf(page);
  }
  return waitFor(async () => !(await windows()).some(w => w.windowId === windowId), 10000);
}

let serial = 0;
async function secondWindowCase(name, { compact = false, fullscreen = false, right = false,
                                        multiple = false } = {}) {
  const tornBefore = torn(), goneBefore = gone();
  if (compact) await setKoanPref('koan.compact_mode.enabled', true);
  if (multiple) await setKoanPref('koan.single_toolbar', false);
  if (right) await setKoanPref('koan.tabs.on_right', true);
  const w = await openWindow(server.url(`/p?t=Second%20${++serial}`));
  check(`${name}: a second window opened`, !!w.windowId && (await windows()).length === 2,
        `${(await windows()).length} windows`);
  if (fullscreen && IS_MAC) {
    await setWindowState(w.windowId, 'fullscreen');
    const full = await waitFor(async () => (await windowOf(w.targetId))?.bounds?.windowState === 'fullscreen', 8000);
    check(`${name}: ...in native fullscreen`, !!full);
    await sleep(2000);
  }
  const closed = await closeWindow(w.windowId, name);
  await sleep(1500);
  check(`${name}: Close Window closed it`, !!closed, `${(await windows()).length} windows left`);
  check(`${name}: Koan keeps running`, await alive());
  check(`${name}: "window torn down" logged once more`, torn() === tornBefore + 1,
        `${tornBefore} -> ${torn()}`);
  check(`${name}: ...never "(widget gone)"`, gone() === goneBefore, `${goneBefore} -> ${gone()}`);
  check(`${name}: nothing fatal in the log`, !fatalInLog(), fatalInLog());
  if (compact) await setKoanPref('koan.compact_mode.enabled', false);
  if (right) await setKoanPref('koan.tabs.on_right', false);
  if (multiple) await setKoanPref('koan.single_toolbar', true);
  await sleep(800);
}

await secondWindowCase('Single toolbar');
await secondWindowCase('Compact mode', { compact: true });
if (IS_MAC) {
  await secondWindowCase('Fullscreen', { fullscreen: true });
  await secondWindowCase('Compact + fullscreen', { compact: true, fullscreen: true });
}
await secondWindowCase('Tabs on the right', { right: true });
await secondWindowCase('Multiple toolbars', { multiple: true });

// The last tab of a second window closed: like Zen (browser.tabs.
// closeWindowWithLastTab is off), the window stays, on Zen's empty tab
// (KeepWindowOnLastTabClosed()), and nothing is torn down. Then Close Window
// from that tab (navigated to a local page, which can ask test_helper).
{
  const tornBefore = torn(), goneBefore = gone();
  const w = await openWindow(server.url('/p?t=Last%20tab'));
  await browserCall('Target.closeTarget', { targetId: w.targetId });
  const empty = await waitFor(async () => (await windows()).find(x => x.windowId === w.windowId)
      ?.pages.find(p => !p.url.startsWith('devtools:')), 8000);
  check('Last tab of a second window: the window stays, on a new tab', !!empty,
        `${(await windows()).length} windows`);
  check('...Koan keeps running', await alive());
  check('...nothing torn down', torn() === tornBefore && gone() === goneBefore,
        `torn ${tornBefore} -> ${torn()}, gone ${goneBefore} -> ${gone()}`);
  if (empty) {
    await onPage(empty, c => c.send('Page.navigate',
                                    { url: server.url('/p?t=Last%20tab%20again') }));
    await sleep(1500);
    const page = (await windows()).find(x => x.windowId === w.windowId)?.pages
        .find(p => !p.url.startsWith('devtools:'));
    const closed = page && await closeWindow(w.windowId, 'Last tab of a second window');
    await sleep(1500);
    check('...Close Window then closes it', !!closed, `${(await windows()).length} windows left`);
    check('..."window torn down" once more, no "(widget gone)"',
          torn() === tornBefore + 1 && gone() === goneBefore,
          `torn ${tornBefore} -> ${torn()}, gone ${goneBefore} -> ${gone()}`);
  }
}

// Quit with two windows: both torn down in order, the browser exits.
{
  const tornBefore = torn(), goneBefore = gone();
  await openWindow(server.url('/p?t=Quit%20two'));
  check('Quit: two windows open', (await windows()).length === 2);
  const first = (await pages()).find(p => !p.url.includes('Quit%20two') && p.type === 'page');
  if (IS_MAC) {
    // (Cmd+Q is the main menu's: some window of Koan's must be key.)
    if (!(await makeKey(first))) console.log('  (the first window\'s page never got the focus)');
    checkMenuItem('Quit: the app menu\'s Quit is enabled', QUIT_ITEM);
  }
  if (realInput()) {
    await realKey('q', 'c', 500);
  } else if (IS_MAC) {
    // Cmd+Q is the menu's too: CDP's Browser.close, which quits as the
    // menu's Quit does (chrome::ExitIgnoreUnloadHandlers()).
    await browserCall('Browser.close').catch(() => {});
  } else {
    // Chromium's Ctrl+Shift+Q.
    await key(first, 'q', ACCEL | SHIFT, 500).catch(() => {});
  }
  const exited = await waitFor(async () => !(await alive()), 30000);
  check('Quit: Koan exits', !!exited);
  check('Quit: both windows logged "window torn down"', torn() >= tornBefore + 2,
        `${tornBefore} -> ${torn()}`);
  check('Quit: ...never "(widget gone)"', gone() === goneBefore, `${goneBefore} -> ${gone()}`);
  check('Quit: nothing fatal in the log', !fatalInLog(), fatalInLog());
}

server.close();
finish();
