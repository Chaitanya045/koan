// Copy current URL with several tabs selected in the sidebar: the command
// palette's "Copy Current URL" (Cmd+Shift+C's action) copies every selected
// tab's URL, one per line in the sidebar's order, and confirms with the
// plural toast; with one tab selected it copies that tab's URL as before.
// Tabs are selected through the test_helper extension (chrome.tabs.highlight,
// the model's multi-selection) and the action runs from the palette in the
// sidebar's URL bar (no menu-bar key: those don't reach on macOS).
//
//   scripts/mac/suite.sh copy_urls   (its .flags load test_helper)
import { browserCall, check, evalIn, finish, front, pages, sleep, typeInto,
         viewTree, waitFor } from './lib/koan.mjs';
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

// Runs "Copy Current URL" from the palette, typed into the sidebar's URL
// bar, then reads the clipboard from `from` (a 127.0.0.1 page).
async function copyFromPalette(from) {
  await front(from);
  const vs = await viewTree();
  const pill = vs.find(v => v.cls === 'KoanUrlPill' && v.shown);
  if (!pill) return { error: 'no URL pill' };
  await clickThroughRoot(vs, pill);
  const field = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'OmniboxViewViews' && v.shown), 4000);
  if (!field) return { error: 'no URL bar' };
  await typeInto(field, '>Copy Current URL', 13);
  await sleep(800);
  const toast = (await viewTree()).filter(v => v.cls === 'Label' && v.shown &&
      /^Copied /.test(v.props?.Text ?? '')).map(v => v.props.Text)[0] ?? '';
  await front(from);
  const text = await evalIn(from, 'navigator.clipboard.readText().catch(e => "ERR " + e)', true);
  return { text, toast };
}

// Two tabs selected, picked in reverse: the clipboard keeps the sidebar's order.
let page = await pageAt(C);
check('test_helper selected Gamma and Alpha',
      await askTestHelper(page, { highlight: [C, A] }, 1200));
let r = await copyFromPalette(await pageAt(C));
check('Two selected tabs: both URLs, one per line, in the sidebar\'s order',
      r.text === `${A}\n${C}`, JSON.stringify(r));
check('...with the plural toast', !r.toast || r.toast === 'Copied 2 URLs!',
      JSON.stringify(r.toast));

// One tab selected: just its URL, Zen's toast.
page = await pageAt(B);
await askTestHelper(page, { highlight: [B] }, 1200);
r = await copyFromPalette(await pageAt(B));
check('One selected tab: only its URL', r.text === B, JSON.stringify(r));
check('...with Zen\'s toast', !r.toast || r.toast === 'Copied current URL!',
      JSON.stringify(r.toast));

server.close();
finish();
