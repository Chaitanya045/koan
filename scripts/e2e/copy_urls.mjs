// Copy current URL with several tabs selected in the sidebar: the command
// palette's "Copy Current URL" (Cmd+Shift+C's action) copies every selected
// tab's URL, one per line in the sidebar's order, and confirms with the
// plural toast; with one tab selected it copies that tab's URL as before.
// Tabs are selected through the test_helper extension (chrome.tabs.highlight,
// the model's multi-selection) and the action runs from the palette in the
// sidebar's URL bar (no menu-bar key: those don't reach on macOS).
//
//   scripts/mac/suite.sh copy_urls   (its .flags load test_helper)
import { execSync } from 'node:child_process';
import { IS_MAC, browserCall, check, evalIn, finish, front, pages, sleep, typeInto,
         viewEvent, viewTree, waitFor } from './lib/koan.mjs';
import { askTestHelper, clickThroughRoot, startServer } from './lib/mac.mjs';

const server = await startServer();
const url = t => server.url(`/p?t=${t}`);
const A = url('Alpha'), B = url('Beta'), C = url('Gamma');
for (const u of [A, B, C]) {
  await browserCall('Target.createTarget', { url: u });
  await sleep(800);
}
const pageAt = async u => (await pages()).find(p => p.url === u);
await waitFor(async () => !!await pageAt(C), 8000);
await browserCall('Browser.grantPermissions', {
  origin: server.url('').replace(/\/$/, ''),
  permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'] });

// The palette's rows: each shown OmniboxResultView's first text (as in
// palette.mjs).
async function rowLabels() {
  const out = [];
  let row = null;
  for (const v of await viewTree()) {
    if (v.cls === 'OmniboxResultView') {
      row = v.shown ? { texts: [] } : null;
      if (row) out.push(row);
    } else if (row && v.cls === 'OmniboxTextView' && v.shown && v.props?.Text?.trim()) {
      row.texts.push(v.props.Text.trim());
    }
  }
  return out.filter(r => r.texts.length).map(r => r.texts[0]);
}

// The clipboard's text: pbpaste on macOS (a page's clipboard read needs the
// window to be key), else navigator.clipboard from `from`.
async function clipboardText(from) {
  if (IS_MAC) return execSync('pbpaste', { encoding: 'utf8' });
  await front(from);
  return evalIn(from, 'navigator.clipboard.readText().catch(e => "ERR " + e)', true);
}

// Runs "Copy Current URL" from the palette, typed into the sidebar's URL
// bar: the action row must be the first (Enter runs it only then), and
// Enter goes in only once it is. Returns the clipboard and the toast.
async function copyFromPalette(from) {
  if (IS_MAC) execSync('pbcopy', { input: '' });
  await front(from);
  const vs = await viewTree();
  const pill = vs.find(v => v.cls === 'KoanUrlPill' && v.shown);
  if (!pill) return { error: 'no URL pill' };
  await clickThroughRoot(vs, pill);
  await sleep(400);  // the click's select-all is posted
  const field = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'OmniboxViewViews' && v.shown), 4000);
  if (!field) return { error: 'no URL bar' };
  // Zen's clickSelectsAll: the click selects the whole address, so the
  // query replaces it (urlbar.mjs checks the selection too).
  const text = field.props?.Text ?? '';
  const sel = (field.props?.SelectedRange ?? '').match(/\d+/g)?.map(Number) ?? [];
  if (!text || Math.min(...sel) !== 0 || Math.max(...sel) !== text.length) {
    return { error: 'the pill click did not select the address', text, sel: field.props?.SelectedRange };
  }
  await typeInto(field, '>Copy Current URL');
  const typed = (await viewTree()).find(v => v.cls === 'OmniboxViewViews' && v.shown)?.props?.Text;
  if (typed !== '>Copy Current URL') return { error: 'typing did not replace the address', typed };
  const rows = await waitFor(async () => {
    const rs = await rowLabels();
    return rs[0] === 'Copy Current URL' && rs;
  }, 5000) || await rowLabels();
  if (rows[0] !== 'Copy Current URL') return { error: 'no action row first', rows };
  for (const type of ['keyPressed', 'keyReleased']) {
    await viewEvent(field, 'DOM.dispatchKeyEvent',
                    { type, keyCode: 13, code: 0, flags: 0, key: 0, isChar: false });
  }
  const toast = await waitFor(async () => (await viewTree()).find(v => v.cls === 'Label' &&
      v.shown && /^Copied /.test(v.props?.Text ?? ''))?.props.Text, 2500) ?? '';
  return { text: await clipboardText(from), toast };
}

// Two tabs selected, picked in reverse: the clipboard keeps the sidebar's order.
let page = await pageAt(C);
check('test_helper selected Gamma and Alpha',
      await askTestHelper(page, { highlight: [C, A] }, 1200));
let r = await copyFromPalette(await pageAt(C));
check('Two selected tabs: both URLs, one per line, in the sidebar\'s order',
      r.text === `${A}\n${C}`, JSON.stringify(r));
check('...with the plural toast', r.toast === 'Copied 2 URLs!',
      JSON.stringify(r.toast));

// One tab selected: just its URL, Zen's toast.
page = await pageAt(B);
await askTestHelper(page, { highlight: [B] }, 1200);
r = await copyFromPalette(await pageAt(B));
check('One selected tab: only its URL', r.text === B, JSON.stringify(r));
check('...with Zen\'s toast', r.toast === 'Copied current URL!',
      JSON.stringify(r.toast));

server.close();
finish();
