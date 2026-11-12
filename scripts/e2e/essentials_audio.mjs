// End-to-end test of Zen's audio badge on an Essentials tile against a real
// build: an Essential playing audio keeps its favicon (upstream would show
// its alert indicator instead), with a 16px round badge over the favicon's
// top-right corner (Firefox's .tab-icon-overlay: top -7px, inset-inline-end
// -7px); clicking the badge mutes the tab without selecting it (the muted
// glyph, "Unmute tab"), clicking again unmutes it, and once the audio stops
// the badge goes and the favicon stays.
//
// On macOS the tab's menu is a native NSMenu, which UI DevTools can't reach
// (NATIVE_MENUS in lib/koan.mjs): the test_helper extension
// (essentials_audio.flags) pins the tab instead of "Add to Essentials".
//
//   scripts/e2e/run.sh essentials_audio
import { servePages } from '../perf/pages_server.mjs';
import {
  browserCall, check, evalIn, finish, mouseOn, onPage, pages, sleep, tabMenuCommand, viewTree,
  visiblePage, waitFor, NATIVE_MENUS,
} from './lib/koan.mjs';
import { askTestHelper } from './lib/mac.mjs';

const site = await servePages();
const MEDIA = `${site.origin}/media.html`;
const media = async () => (await pages()).find(p => p.url === MEDIA);

// The Essentials tile's views: its tab view, favicon, upstream's alert
// indicator and Koan's badge (one Essential in this test).
async function tile() {
  const vs = (await viewTree()).filter(v => v.path.includes('PinnedTabContainerView'));
  const one = cls => vs.find(v => v.cls === cls && v.path.at(-1) === 'TabView');
  return { tab: vs.find(v => v.cls === 'TabView' && v.shown), icon: one('TabIcon'),
           alert: one('AlertIndicatorButton'), badge: one('KoanEssentialsAudioBadge') };
}
// Where the favicon is drawn: the middle 16x16 of the TabIcon (its border
// keeps room for the attention dot).
const favicon = icon => ({ x: icon.ax + (icon.w - 16) / 2, y: icon.ay + (icon.h - 16) / 2 });
const describe = t => JSON.stringify({
  icon: t.icon && [t.icon.shown, t.icon.ax, t.icon.ay, t.icon.w, t.icon.h],
  alert: t.alert && [t.alert.shown, t.alert.w],
  badge: t.badge && [t.badge.shown, t.badge.ax, t.badge.ay, t.badge.w, t.badge.h,
                     t.badge.props.AudioState, t.badge.props.TooltipText],
});

// The media page, made an Essential.
const start = await visiblePage();
await onPage(start, c => c.send('Page.navigate', { url: MEDIA }));
await waitFor(media);
await sleep(1500);
await evalIn(await media(), 'document.title = "Essential audio"');
await sleep(500);
if (NATIVE_MENUS) {
  await askTestHelper(await media(), 'pin-tab', 1500);
} else {
  await tabMenuCommand({ title: 'Essential audio' }, 'kKoanAddToEssentialsMenuItem');
  await sleep(1000);
}
let t = await tile();
check('The media tab is an Essentials tile', t.tab?.shown, describe(t));
check('...with no badge while it is silent', t.icon?.shown && !t.badge?.shown, describe(t));

// It plays: the favicon stays, the badge shows at its top-right corner.
const playing = await evalIn(await media(), 'song.play().then(() => !song.paused)', true);
check('The page plays its song', playing === true);
t = await waitFor(async () => {
  const now = await tile();
  return now.badge?.shown ? now : null;
}, 5000) || await tile();
check('A playing Essential keeps its favicon (not upstream\'s alert in its place)',
      t.icon?.shown && t.icon.w > 0 && !(t.alert?.shown && t.alert.w > 0), describe(t));
check('...with the audio badge shown, playing, "Mute tab"',
      t.badge?.shown && t.badge.props.AudioState === 'playing' &&
      t.badge.props.TooltipText === 'Mute tab', describe(t));
const f = t.icon && favicon(t.icon);
check('...16px, 7px up and 7px right of the favicon (top: -7px; inset-inline-end: -7px)',
      f && t.badge?.w === 16 && t.badge.h === 16 &&
      t.badge.ax === f.x + 7 && t.badge.ay === f.y - 7,
      `favicon ${f && [f.x, f.y]}; ${describe(t)}`);
check('...inside the tile', t.tab && t.badge && t.badge.ax >= t.tab.ax &&
      t.badge.ay >= t.tab.ay && t.badge.ax + 16 <= t.tab.ax + t.tab.w, describe(t));

// Another tab in front: the Essential plays in the background, still badged.
await browserCall('Target.createTarget', { url: 'about:blank' });
await sleep(1500);
t = await tile();
check('Unselected, the playing Essential still shows its favicon and badge',
      t.icon?.shown && t.badge?.shown && t.badge.props.AudioState === 'playing', describe(t));

// Clicking the badge mutes the tab, without selecting it.
await mouseOn(t.badge, 'mousePressed', 8, 8);
await mouseOn(t.badge, 'mouseReleased', 8, 8);
t = await waitFor(async () => {
  const now = await tile();
  return now.badge?.props.AudioState === 'muted' ? now : null;
}, 3000) || await tile();
check('Clicking the badge mutes the tab: the muted glyph, "Unmute tab"',
      t.badge?.shown && t.badge.props.AudioState === 'muted' &&
      t.badge.props.TooltipText === 'Unmute tab', describe(t));
check('...and doesn\'t select the Essential',
      await evalIn(await media(), 'document.visibilityState') === 'hidden');
check('...and the favicon stays', t.icon?.shown && t.icon.w > 0, describe(t));

// Clicking it again unmutes it.
await mouseOn(t.badge, 'mousePressed', 8, 8);
await mouseOn(t.badge, 'mouseReleased', 8, 8);
t = await waitFor(async () => {
  const now = await tile();
  return now.badge?.props.AudioState === 'playing' ? now : null;
}, 3000) || await tile();
check('Clicking it again unmutes the tab', t.badge?.props.AudioState === 'playing',
      describe(t));

// Once the audio stops (Chromium keeps a tab "recently audible" for 2s), the
// badge goes and the favicon stays.
await evalIn(await media(), 'song.pause()');
t = await waitFor(async () => {
  const now = await tile();
  return now.badge && !now.badge.shown ? now : null;
}, 6000) || await tile();
check('When the audio stops, the badge goes and the favicon stays',
      !t.badge?.shown && t.icon?.shown && t.icon.w > 0, describe(t));

site.close();
finish();
