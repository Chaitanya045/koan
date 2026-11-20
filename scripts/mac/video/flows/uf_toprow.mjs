// User flow: the sidebar's top row and the URL pill's extras (#400). Test
// cases UF-TOP-* in .agent/notes/user-flows.md. Back, forward and reload
// with REAL clicks (they were window caption on the Mac: a real click
// dragged the window and never reached them, while UI DevTools clicks
// passed), the window not moving; Copy URL, the pinned extension's button
// and the Extensions button fading in on the sidebar's hover (pixels), the
// extension's popup, Copy URL's toast and clipboard, Chromium's extensions
// menu; and none of the extras on an empty tab or while the address is
// edited. run_flows.sh loads scripts/e2e/extensions/action_popup (badge
// "135") and pins it (scripts/e2e/prefs/sidebar_top_row.json).
import { execSync } from 'node:child_process';
import { CDP_PORT, evalIn, onPage } from '../../../e2e/lib/koan.mjs';
import { begin, browserCall, clickView, diffPixels, end, hoverView, keyPress, openAnchor, pageById,
         parkPointer, pointer, show, sleep, step, viewRect, viewTree, waitFor, windowBounds } from './uf_lib.mjs';

const EXTENSION_ID = 'fpfndddeinnmcgdmdlibaobomgbdjljd';
const fixtures = await begin('uf_toprow', 'The top row\'s buttons take real clicks; the URL pill\'s extras', '#400');
const A = fixtures.url('/p?t=Alpha'), B = fixtures.url('/p?t=Beta');
const anchor = await openAnchor(A);
const page = await pageById(anchor.id);
const href = () => evalIn(page, 'location.href');
await onPage(page, c => c.send('Page.navigate', { url: B }));
await waitFor(async () => (await href()) === B, 8000);
await sleep(800);
await parkPointer();

const navButtons = vs => vs.filter(v => v.shown && v.path.at(-1) === 'KoanSidebarNavButtons').sort((a, b) => a.ax - b.ax);
const pillButton = (vs, tip) => vs.find(v => v.shown && v.path.includes('KoanUrlPill') && v.props?.TooltipText === tip);
const extensionButton = vs => vs.find(v => v.shown && v.path.at(-1) === 'KoanSidebarExtensionsView');
const same = (a, b) => a && b && a.left === b.left && a.top === b.top && a.width === b.width && a.height === b.height;

await step('UF-TOP-1', 'Back, forward and reload with real clicks; the window stays put', async t => {
  let vs = await viewTree();
  const row = navButtons(vs);
  t.check('the top row has back, forward and reload', row.length === 3, row.map(v => v.props?.TooltipText).join(', '));
  if (row.length !== 3) return;
  const w0 = await windowBounds();
  await t.shot('before');
  let c = await clickView(row[0], vs, 900);
  t.check('Back (a real click) goes back to Alpha', c.ok && !!(await waitFor(async () => (await href()) === A, 5000)),
          c.why ?? await href());
  await t.shot('back');
  vs = await viewTree();
  c = await clickView(navButtons(vs)[1], vs, 900);
  t.check('Forward (a real click) goes to Beta', c.ok && !!(await waitFor(async () => (await href()) === B, 5000)),
          c.why ?? await href());
  await evalIn(page, 'window.__ufMark = 1');
  vs = await viewTree();
  c = await clickView(navButtons(vs)[2], vs, 900);
  t.check('Reload (a real click) reloads Beta',
          c.ok && !!(await waitFor(async () => (await evalIn(page, 'window.__ufMark').catch(() => 1)) === undefined, 5000)) &&
          (await href()) === B, c.why ?? '');
  t.check('the window did not move (the buttons are client area, not caption)', same(w0, await windowBounds()),
          `${JSON.stringify(w0)} -> ${JSON.stringify(await windowBounds())}`);
}, { needs: ['real input'] });

const pillRect = async () => {
  const vs = await viewTree();
  const pill = vs.find(v => v.cls === 'KoanUrlPill' && v.shown);
  return pill && { pill, vs, r: await viewRect(pill, vs) };
};
await step('UF-TOP-2', 'The pill\'s extras fade in while the sidebar is hovered (pixels)', async t => {
  await parkPointer();
  await sleep(500);
  const p = await pillRect();
  t.check('the URL pill is there', !!p?.r);
  if (!p?.r) return;
  const rest = await t.shot('at-rest', { pixels: true });
  // Over the sidebar, below the pill (not on it): Zen's implicit hover.
  await pointer('move', Math.round(p.r.x + 30), Math.round(p.r.y + p.r.h + 120), 200);
  await sleep(500);
  const hovered = await t.shot('sidebar-hovered', { pixels: true });
  const trailing = { x: p.r.x + p.r.w * 0.55, y: p.r.y + 2, w: p.r.w * 0.45 - 2, h: p.r.h - 4 };
  if (rest.frame && hovered.frame) {
    const d = diffPixels(rest, hovered, trailing);
    t.check('the pixels: the pill\'s trailing end changed on the sidebar\'s hover (the buttons faded in)',
            d.changed > 30, `${d.changed} of ${d.n} px changed`);
  } else {
    t.check('screenshots', false, rest.error ?? hovered.error);
  }
  const vs = await viewTree();
  t.check('the pinned extension\'s button, Copy URL and Extensions are in the pill',
          !!extensionButton(vs) && !!pillButton(vs, 'Copy URL') && !!pillButton(vs, 'Extensions'),
          `extension ${!!extensionButton(vs)}, copy ${!!pillButton(vs, 'Copy URL')}, extensions ${!!pillButton(vs, 'Extensions')}`);
}, { needs: ['real input', 'screen recording'] });

await step('UF-TOP-3', 'The pinned extension\'s button (badge "135") opens its popup', async t => {
  const vs = await viewTree();
  const ext = extensionButton(vs);
  t.check('the extension\'s button shows', !!ext);
  if (!ext) return;
  await hoverView(ext, vs);
  await t.shot('badge');
  const c = await clickView(ext, vs, 1200);
  const popup = c.ok && await waitFor(async () => (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .find(x => x.url === `chrome-extension://${EXTENSION_ID}/popup.html`), 5000);
  t.check('a real click opens the extension\'s popup', !!popup, c.why ?? '');
  await t.shot('popup');
  await keyPress('Escape', '', 600);
}, { needs: ['real input'] });

await step('UF-TOP-4', 'Copy URL: the address on the clipboard and Zen\'s toast', async t => {
  execSync('pbcopy', { input: '' });
  let vs = await viewTree();
  const copy = pillButton(vs, 'Copy URL');
  t.check('Copy URL is in the pill', !!copy);
  if (!copy) return;
  await hoverView(copy, vs);
  const c = await clickView(copy, vs, 250);
  t.check('a real click on it', c.ok, c.why);
  await sleep(500);
  vs = await viewTree();
  const toast = vs.find(v => v.cls === 'Label' && v.shown && /^Copied /.test(v.props?.Text ?? ''))?.props.Text ?? '';
  t.check('the toast says "Copied current URL!"', toast === 'Copied current URL!', JSON.stringify(toast));
  await t.shot('copied');
  const text = execSync('pbpaste', { encoding: 'utf8' });
  t.check('the clipboard has the page\'s address', text === B, JSON.stringify(text));
}, { needs: ['real input'] });

await step('UF-TOP-5', 'The Extensions button opens Chromium\'s extensions menu; a second click closes it', async t => {
  const menuShown = async () => (await viewTree()).some(v => v.shown && /^ExtensionsMenu(MainPage)?View$/.test(v.cls));
  let vs = await viewTree();
  const ext = pillButton(vs, 'Extensions');
  t.check('the Extensions button is in the pill', !!ext);
  if (!ext) return;
  const w0 = await windowBounds();
  await hoverView(ext, vs);
  let c = await clickView(ext, vs, 900);
  t.check('a real click opens the extensions menu', c.ok && !!(await waitFor(menuShown, 4000)), c.why ?? '');
  await t.shot('menu');
  vs = await viewTree();
  c = await clickView(pillButton(vs, 'Extensions') ?? ext, vs, 900);
  t.check('a second real click closes it', c.ok && !!(await waitFor(async () => !(await menuShown()), 4000)));
  t.check('the window did not move', same(w0, await windowBounds()));
}, { needs: ['real input'] });

await step('UF-TOP-6', 'While the address is edited, the extras are gone', async t => {
  const p = await pillRect();
  if (!p?.r) return t.check('the URL pill is there', false);
  const c = await clickView(p.pill, p.vs, 700);
  t.check('a real click on the pill edits the address', c.ok, c.why);
  const vs = await viewTree();
  t.check('no Copy URL and no Extensions button while editing',
          !pillButton(vs, 'Copy URL') && !pillButton(vs, 'Extensions'));
  await t.shot('editing');
  await keyPress('Escape', '', 600);
}, { needs: ['real input'] });

await step('UF-TOP-7', 'On an empty tab the extras are gone, even with the sidebar hovered', async t => {
  const id = (await browserCall('Target.createTarget', { url: 'chrome://newtab/' })).result?.targetId;
  await sleep(1500);
  await show(id);
  const p = await pillRect();
  if (p?.r) {
    await pointer('move', Math.round(p.r.x + 30), Math.round(p.r.y + p.r.h + 120), 200);
    await sleep(600);
  }
  const vs = await viewTree();
  t.check('no Copy URL and no Extensions button on the empty tab',
          !pillButton(vs, 'Copy URL') && !pillButton(vs, 'Extensions'));
  await t.shot('empty-tab');
  await browserCall('Target.closeTarget', { targetId: id }).catch(() => {});
  await show(anchor.id);
  await parkPointer();
}, { needs: ['real input'] });

end();
