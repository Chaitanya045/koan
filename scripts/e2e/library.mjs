// End-to-end test of the Library against a real build: its shortcuts
// (Alt+Shift+L, Ctrl+H, Ctrl+Shift+Y) and sidebar button, History (search,
// filters, forget, open), Downloads (a real download, its actions and type
// filters, a dangerous one) and Spaces (rename, reorder), and closing with
// Esc. What History deletes is checked in library_history.mjs.
//
//   scripts/e2e/run.sh library     (opens scripts/e2e/pages/library.html)
import { copyFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import {
  ALT, CTRL, SHIFT, UI_PORT, browserCall, check, connect, createSpace, evalIn, finish,
  key as keyTo, pages as tabs, setKoanPref, sleep, visiblePage, waitFor, IS_MAC, META,
} from './lib/koan.mjs';
// Library Downloads: Ctrl+Shift+Y, on macOS Cmd+J (koan_shortcut_table.cc).
const DOWNLOADS_KEY = IS_MAC ? [74, META, 'j'] : [89, CTRL | SHIFT, 'Y'];
// Library History: Ctrl+H, on macOS Cmd+Y (koan_shortcut_table.cc).
const HISTORY_KEY = IS_MAC ? [89, META, 'y'] : [72, CTRL, 'h'];

// With SHOTS=<dir> (see run.sh), copies the window's latest off-screen frame
// (the largest dump) there as <name>.png.
async function shot(name) {
  const SHOTS = process.env.SHOTS;
  if (!SHOTS) return;
  await sleep(800);
  const dir = `${SHOTS}/frames`;
  const frames = readdirSync(dir).filter(f => f.endsWith('.png'))
      .map(f => `${dir}/${f}`).sort((a, b) => statSync(b).size - statSync(a).size);
  if (frames.length) copyFileSync(frames[0], `${SHOTS}/${name}.png`);
}

// The native views of interest, with their properties, ancestry and whether
// they are shown. Library views live in the Library's own widget.
const INTEREST = /^(KoanLibrary\w*|Space\w+|MoveHandle|RenameField|Textfield|LabelButton|ImageButton|KoanSpaceButton)$/;
async function views() {
  const ui = await connect(`ws://127.0.0.1:${UI_PORT}/0`);
  await ui.send('DOM.enable');
  await ui.send('CSS.enable');
  const doc = (await ui.send('DOM.getDocument', { depth: -1 })).result.root;
  const nodes = [];
  const walk = async (n, shown, path) => {
    const a = n.attributes ?? [];
    const cls = n.nodeName === 'View' ? a[a.indexOf('class') + 1] : null;
    const inLibrary = path.includes('KoanLibraryView');
    if (cls && INTEREST.test(cls) &&
        (inLibrary || /^Koan(Library|Space)/.test(cls))) {
      const r = await ui.send('CSS.getMatchedStylesForNode', { nodeId: n.nodeId });
      const p = {};
      for (const rule of r.result?.matchedCSSRules ?? []) {
        for (const prop of rule.rule.style.cssProperties) p[prop.name] = prop.value;
      }
      shown = shown && p.Visible !== 'false';
      nodes.push({ cls, nodeId: n.nodeId, p, shown, path });
    }
    const next = cls ? [...path, cls] : path;
    for (const c of n.children ?? []) await walk(c, shown, next);
  };
  await walk(doc, true, []);
  return { nodes, ui };
}

// Runs `fn` on a fresh snapshot of the views and closes it.
async function withViews(fn) {
  const { nodes, ui } = await views();
  try { return await fn(nodes, ui); } finally { ui.close(); }
}

const find = (nodes, cls, pred = () => true) =>
  nodes.filter(n => n.cls === cls && pred(n));

async function click(ui, node, { x, y } = {}) {
  const px = x ?? Math.floor((+node.p.Width || 10) / 2);
  const py = y ?? Math.floor((+node.p.Height || 10) / 2);
  for (const type of ['mousePressed', 'mouseReleased']) {
    await ui.send('DOM.dispatchMouseEvent', {
      nodeId: node.nodeId,
      event: { type, x: px, y: py, button: 'left', wheelDirection: 'none' },
    });
  }
}

async function mouse(ui, node, type, x, y, button = 'left') {
  await ui.send('DOM.dispatchMouseEvent', {
    nodeId: node.nodeId,
    event: { type, x, y, button, wheelDirection: 'none' },
  });
}

// Keys straight to a native view (a textfield). `text` types characters.
async function typeInto(ui, node, text) {
  for (const ch of text) {
    await ui.send('DOM.dispatchKeyEvent', {
      nodeId: node.nodeId,
      event: { type: 'keyPressed', keyCode: 0, code: 0, flags: 0, key: ch.charCodeAt(0), isChar: true },
    });
  }
}
async function keyInto(ui, node, keyCode, flags = 0) {
  for (const type of ['keyPressed', 'keyReleased']) {
    await ui.send('DOM.dispatchKeyEvent', {
      nodeId: node.nodeId,
      event: { type, keyCode, code: 0, flags, key: 0, isChar: false },
    });
  }
}

// Keys to the page that is showing, which passes unhandled ones on to the
// browser like real input (the browser's shortcuts).
async function key(code, modifiers, keyName = '') {
  await keyTo(await visiblePage(), code, modifiers, keyName);
  await sleep(900);
}

const KEY_L = 76, KEY_H = 72, KEY_Y = 89, ESC = 27;

const library = async () => withViews(nodes => {
  const view = find(nodes, 'KoanLibraryView')[0];
  const section = ['KoanLibraryHistorySection', 'KoanLibraryDownloadsSection',
                   'KoanLibrarySpacesSection']
      .map(cls => find(nodes, cls, n => n.shown)[0]).find(Boolean);
  return {
    open: view?.p.Open === 'true',
    width: +(view?.p.Width ?? 0),
    section: section?.cls ?? null,
    // Where the section starts in the panel: after the side column, or at
    // its left edge with the panel mirrored.
    sectionX: +(section?.p.X ?? -1),
    rows: +(section?.p.RowCount ?? section?.p.CardCount ?? -1),
  };
});

// --- Some history: three copies of the test page. ---
const first = (await tabs())[0];
const pageUrl = first.url.split('?')[0];
for (const q of ['two', 'three']) {
  const c = await connect(first.webSocketDebuggerUrl);
  await c.send('Page.navigate', { url: `${pageUrl}?${q}` });
  c.close();
  await sleep(1500);
}

// --- Alt+Shift+L opens the Library on History (Zen's default tab). ---
await key(KEY_L, ALT | SHIFT, 'L');
let state = await waitFor(async () => {
  const s = await library();
  return s.open && s.rows >= 3 ? s : null;
}) ?? await library();
check('Alt+Shift+L opens the Library', state.open);
check('...on History, listing the visited pages',
      state.section === 'KoanLibraryHistorySection' && state.rows >= 3,
      `${state.section}, ${state.rows} row(s)`);
check('...at Zen\'s width (418px)', state.width === 418, `width ${state.width}`);
await shot('library-history');

// --- Search narrows the list. ---
await withViews(async (nodes, ui) => {
  const field = find(nodes, 'Textfield',
                     n => n.path.includes('KoanLibraryHistorySection'))[0];
  await typeInto(ui, field, 'three');
});
state = await waitFor(async () => {
  const s = await library();
  return s.rows === 1 ? s : null;
}) ?? await library();
check('Searching History narrows the list', state.rows === 1, `${state.rows} row(s)`);
const searchTitle = await withViews(nodes =>
  find(nodes, 'KoanLibraryRow', n => n.shown)[0]?.p.Title);
check('...to the matching page', /three/.test(searchTitle ?? ''), searchTitle);
await withViews(async (nodes, ui) => {
  const field = find(nodes, 'Textfield',
                     n => n.path.includes('KoanLibraryHistorySection'))[0];
  await keyInto(ui, field, 65, 4);  // Ctrl+A
  await keyInto(ui, field, 8);      // Backspace
});
state = await waitFor(async () => {
  const s = await library();
  return s.rows >= 3 ? s : null;
}) ?? await library();
check('Clearing the search lists everything again', state.rows >= 3, `${state.rows} row(s)`);

// --- The filter panel: sort by site groups pages under "Other" (file://). ---
await withViews(async (nodes, ui) => {
  const filter = find(nodes, 'KoanLibraryPill',
                      n => n.shown && n.path.includes('KoanLibraryHistorySection') &&
                           !n.path.includes('WrapView'))
      .find(n => (n.p.Text ?? '').includes('Filter'));
  await click(ui, filter);
});
await sleep(300);
const chips = await withViews(nodes =>
  find(nodes, 'KoanLibraryPill', n => n.shown && n.path.includes('WrapView'))
      .map(n => ({ text: n.p.Text, active: n.p.Active === 'true' })));
check('Filter opens the panel with Zen\'s options',
      ['Today', 'This Week', 'This Month', 'By Date', 'By Site', 'By Most Visited',
       'By Last Visited', 'History', 'Closed tabs']
          .every(t => chips.some(c => c.text === t)),
      chips.map(c => c.text).join(', '));
await shot('library-history-filter');
check('...sorted by date, looking through history',
      chips.find(c => c.text === 'By Date')?.active &&
          chips.find(c => c.text === 'History')?.active);
await withViews(async (nodes, ui) => {
  const site = find(nodes, 'KoanLibraryPill', n => n.shown && n.p.Text === 'By Site')[0];
  await click(ui, site);
});
await sleep(800);
const bySite = await withViews(nodes =>
  find(nodes, 'KoanLibraryPill', n => n.shown && n.p.Text === 'By Site')[0]?.p.Active);
check('Choosing "By Site" makes it the sort order', bySite === 'true');
// Back to dates, and close the panel.
await withViews(async (nodes, ui) => {
  await click(ui, find(nodes, 'KoanLibraryPill', n => n.shown && n.p.Text === 'By Date')[0]);
});
await sleep(500);
await withViews(async (nodes, ui) => {
  await click(ui, find(nodes, 'KoanLibraryPill', n => n.shown && n.p.Text === 'Done')[0]);
});
await sleep(300);

// --- Hovering a row shows its actions; the trash forgets the page. ---
const before = (await library()).rows;
const hovered = await withViews(async (nodes, ui) => {
  const row = find(nodes, 'KoanLibraryRow', n => n.shown)[0];
  await mouse(ui, row, 'mouseEntered', 20, 20);
  return row.p.Title;
});
await sleep(300);
const actions = await withViews(nodes =>
  find(nodes, 'ImageButton', n => n.shown && n.path.includes('KoanLibraryRow'))
      .map(n => n.p.TooltipText ?? n.p.Tooltip));
check('Hovering a history row shows Forget and Reopen',
      actions.includes('Remove from history') && actions.includes('Reopen page'),
      actions.join(', '));
await withViews(async (nodes, ui) => {
  const trash = find(nodes, 'ImageButton',
                     n => n.shown && (n.p.TooltipText ?? n.p.Tooltip) === 'Remove from history')[0];
  await click(ui, trash);
});
state = await waitFor(async () => {
  const s = await library();
  return s.rows === before - 1 ? s : null;
}) ?? await library();
check('Forget removes the page', state.rows === before - 1,
      `${before} -> ${state.rows} row(s), forgot "${hovered}"`);

// --- Clicking a row opens it in a new tab and closes the Library. ---
const tabsBefore = (await tabs()).length;
await withViews(async (nodes, ui) => {
  await click(ui, find(nodes, 'KoanLibraryRow', n => n.shown)[0], { x: 20, y: 20 });
});
await sleep(1500);
state = await library();
check('Opening a page from History adds a tab and closes the Library',
      (await tabs()).length === tabsBefore + 1 && !state.open,
      `${tabsBefore} -> ${(await tabs()).length} tab(s), open ${state.open}`);

// --- Ctrl+H toggles the Library's History. ---
await key(...HISTORY_KEY);
state = await library();
check('Ctrl+H opens History', state.open && state.section === 'KoanLibraryHistorySection');
await key(...HISTORY_KEY);
check('Ctrl+H again closes it', !(await library()).open);

// --- A download, then Ctrl+Shift+Y shows it in Downloads. ---
// Into the test profile (scripts/e2e/prefs/library.json), never the user's
// Downloads.
const profile = process.env.KOAN_CHROME_ARGS.split('\n')
    .find(a => a.startsWith('--user-data-dir=')).slice('--user-data-dir='.length);
const downloads = join(profile, 'Downloads');
{
  const page = (await tabs()).find(t => t.url.startsWith(pageUrl));
  const c = await connect(page.webSocketDebuggerUrl);
  const r = await c.send('Runtime.evaluate', {
    expression: `JSON.stringify(document.getElementById('download').getBoundingClientRect())`,
    returnByValue: true,
  });
  const box = JSON.parse(r.result.result.value);
  const at = { x: box.x + 10, y: box.y + 10, button: 'left', clickCount: 1 };
  await c.send('Input.dispatchMouseEvent', { ...at, type: 'mousePressed' });
  await c.send('Input.dispatchMouseEvent', { ...at, type: 'mouseReleased' });
  c.close();
  await sleep(2000);

  await key(...DOWNLOADS_KEY);
  state = await waitFor(async () => {
    const s = await library();
    return s.open && s.rows >= 1 ? s : null;
  }) ?? await library();
  check('Ctrl+Shift+Y opens Downloads with the download',
        state.open && state.section === 'KoanLibraryDownloadsSection' && state.rows >= 1,
        `${state.section}, ${state.rows} row(s)`);
  const download = await withViews(nodes =>
    find(nodes, 'KoanLibraryRow', n => n.shown)[0]?.p);
  check('...saved in the scratch directory',
        existsSync(join(downloads, 'koan-library-test.txt')));
  check('...named after the file, with its size and source',
        download?.Title === 'koan-library-test.txt' && /B/.test(download?.Subtitle ?? ''),
        `${download?.Title} / ${download?.Subtitle}`);
  await shot('library-downloads');
  await withViews(async (nodes, ui) => {
    await mouse(ui, find(nodes, 'KoanLibraryRow', n => n.shown)[0], 'mouseEntered', 20, 20);
  });
  await sleep(300);
  const downloadActions = await withViews(nodes =>
    find(nodes, 'ImageButton', n => n.shown && n.path.includes('KoanLibraryRow'))
        .map(n => n.p.TooltipText ?? n.p.Tooltip));
  // "Show in Finder" on macOS (koan_library_downloads.cc, as Chrome says it).
  const SHOW_IN_FOLDER = IS_MAC ? 'Show in Finder' : 'Show in folder';
  check(`Hovering a finished download offers ${SHOW_IN_FOLDER} and More`,
        downloadActions.includes(SHOW_IN_FOLDER) && downloadActions.includes('More options'),
        downloadActions.join(', '));

  // File type filters: a .txt is a document, not a picture.
  const chip = async text => withViews(async (nodes, ui) => {
    await click(ui, find(nodes, 'KoanLibraryPill', n => n.shown && n.p.Text === text)[0]);
  });
  await withViews(async (nodes, ui) => {
    const filter = find(nodes, 'KoanLibraryPill',
                        n => n.shown && n.path.includes('KoanLibraryDownloadsSection'))
        .find(n => (n.p.Text ?? '').includes('Filter'));
    await click(ui, filter);
  });
  await sleep(300);
  await chip('Images');
  await sleep(800);
  check('Filtering Downloads by Images hides the text file', (await library()).rows === 0);
  await chip('Documents');
  await sleep(800);
  check('...adding Documents shows it again', (await library()).rows === 1);
  await chip('Images');
  await sleep(300);
  await chip('Documents');
  await sleep(300);
  await chip('Done');
  await sleep(300);

  await key(...DOWNLOADS_KEY);
  check('Ctrl+Shift+Y again closes it', !(await library()).open);
}

// --- A download held back as dangerous (a script saved without a user
// gesture, from a tab of its own): the row says so, and clicking it goes to
// Chrome's warning instead of making it open once it's kept. ---
{
  const targetId = (await browserCall('Target.createTarget',
      { url: `${pageUrl}?danger` })).result.targetId;
  await sleep(1500);
  const page = (await tabs()).find(t => t.id === targetId);
  await evalIn(page, `(() => {
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob(['print(1)'], { type: 'text/x-python' }));
    link.download = 'koan-dangerous.py';
    link.click();
  })()`);
  await sleep(2000);
  const dangerous = async () => withViews(nodes =>
    find(nodes, 'KoanLibraryRow', n => n.shown && n.p.Title === 'koan-dangerous.py')[0]?.p);
  await key(...DOWNLOADS_KEY);
  // Safe Browsing's verdict (library.flags) comes once the file is in.
  let row = await waitFor(async () => {
    const r = await dangerous();
    return /dangerous/.test(r?.Subtitle ?? '') ? r : null;
  }, 15000) ?? await dangerous() ?? {};
  check('A dangerous download is listed with its warning',
        /dangerous/.test(row.Subtitle ?? ''), `${row.Title} / ${row.Subtitle}`);
  await withViews(async (nodes, ui) => {
    const target = find(nodes, 'KoanLibraryRow',
                        n => n.shown && n.p.Title === 'koan-dangerous.py')[0];
    if (target) await click(ui, target, { x: 20, y: 20 });
  });
  await sleep(1500);
  const tabsNow = await tabs();
  // The warning: the download bubble's, or the downloads page.
  check('...clicking it goes to Chrome\'s warning and closes the Library',
        !(await library()).open, tabsNow.map(t => t.url).join(', '));
  if (!(await library()).open) await key(...DOWNLOADS_KEY);
  row = await waitFor(dangerous) ?? {};
  check('...and doesn\'t make it open when it\'s done', /dangerous/.test(row.Subtitle ?? ''),
        `${row.Title} / ${row.Subtitle}`);
  await key(...DOWNLOADS_KEY);
  for (const t of await tabs()) {
    if (t.id === targetId || t.url.startsWith('chrome://downloads')) {
      await browserCall('Target.closeTarget', { targetId: t.id });
    }
  }
  await sleep(1000);
}

// --- Spaces: the window's Space and a new one (Zen's Create form),
// renamed and reordered in the Library. ---
await createSpace(await visiblePage(), 'Second');
await sleep(1000);
// The Library button, at the start of the Spaces' bottom bar, opens it (on
// Downloads, the last section).
const button = await withViews(nodes => find(nodes, 'KoanLibraryButton')[0]);
check('The Library button is in the sidebar\'s bottom bar',
      button?.path.includes('KoanSpaceBarView'), button?.path.join(' > '));
await withViews(async (nodes, ui) => {
  await click(ui, find(nodes, 'KoanLibraryButton')[0]);
});
await sleep(1000);
check('The sidebar\'s Library button opens it', (await library()).open);
await withViews(async (nodes, ui) => {
  const tab = find(nodes, 'KoanLibraryTab', n => n.p.Label === 'Spaces')[0];
  await click(ui, tab);
});
await sleep(1200);
state = await library();
check('The Spaces tab shows a card per space',
      state.section === 'KoanLibrarySpacesSection' && state.rows === 2,
      `${state.section}, ${state.rows} card(s)`);
check('...and the Library grows to fit them', state.width > 418, `width ${state.width}`);
const cardNames = async () => withViews(nodes =>
  find(nodes, 'SpaceCard', n => n.shown).map(n => n.p.Name));
check('...named like the spaces', (await cardNames()).join(',') === 'Space,Second',
      (await cardNames()).join(','));
await shot('library-spaces');

// Rename the first space.
await withViews(async (nodes, ui) => {
  const name = find(nodes, 'SpaceTextButton',
                    n => n.shown && n.path.includes('SpaceCard') &&
                         (n.p.TooltipText ?? n.p.Tooltip) === 'Rename space')[0];
  await click(ui, name);
});
await sleep(500);
await withViews(async (nodes, ui) => {
  const field = find(nodes, 'RenameField', n => n.shown)[0];
  await typeInto(ui, field, 'Work');
  await keyInto(ui, field, 13);
});
await sleep(1000);
check('Renaming a space in its card renames it', (await cardNames())[0] === 'Work',
      (await cardNames()).join(','));
const bar = await withViews(nodes =>
  find(nodes, 'KoanSpaceButton').map(n => n.p.TooltipText ?? n.p.Tooltip));
check('...in the sidebar\'s bottom bar too', bar.includes('Work'), bar.join(', '));

// Drag the first card past the second by its handle.
await withViews(async (nodes, ui) => {
  const handle = find(nodes, 'MoveHandle', n => n.shown)[0];
  await mouse(ui, handle, 'mousePressed', 10, 10);
  await mouse(ui, handle, 'mouseDragged', 150, 10);
  await mouse(ui, handle, 'mouseDragged', 300, 10);
  await mouse(ui, handle, 'mouseReleased', 300, 10);
});
await sleep(1000);
check('Dragging a card by its handle reorders the spaces',
      (await cardNames()).join(',') === 'Second,Work', (await cardNames()).join(','));
const barOrder = await withViews(nodes =>
  find(nodes, 'KoanSpaceButton').map(n => n.p.TooltipText ?? n.p.Tooltip));
check('...in the bottom bar too', barOrder.join(',') === 'Second,Work', barOrder.join(','));

check('...with the side column on the left', state.sectionX === 110,
      `section at x ${state.sectionX}`);

// --- Esc closes the Library. ---
await key(ESC, 0, 'Escape');
check('Esc closes the Library', !(await library()).open);

// --- With tabs on the right, the panel comes from the right, mirrored. ---
await setKoanPref('koan.tabs.on_right', true);
await key(KEY_L, ALT | SHIFT, 'L');
state = await waitFor(async () => {
  const s = await library();
  return s.open ? s : null;
}) ?? await library();
check('With tabs on the right the side column is on the right',
      state.open && state.sectionX === 0, `section at x ${state.sectionX}`);
await shot('library-tabs-right');
await key(ESC, 0, 'Escape');
await setKoanPref('koan.tabs.on_right', false);

finish();
