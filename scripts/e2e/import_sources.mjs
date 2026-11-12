// End-to-end test of the import from Zen, Arc and Chrome (#348), with
// fixture profiles (lib/import_fixtures.mjs, written by setup/import_sources.mjs
// before Koan starts) under the test root (import_sources.flags:
// --koan-import-test-root={profile}/import-root), never the user's own:
// - the welcome's import step lists Zen, Arc and Google Chrome, before
//   Chromium's own sources, each with its app icon (or its initial when the
//   app isn't installed);
// - picking Google Chrome there opens Chromium's import dialog on it;
// - importing from Chrome brings its bookmarks, history and saved password
//   (its Safe Storage key from the test root, never the keychain), and from
//   Zen (Chromium's Firefox importer) its bookmarks and history;
// - "Spaces and pinned tabs" (#363) is offered for Zen and Arc only, in the
//   welcome and in the dialog, and brings their Spaces, pinned tabs and
//   folders (kept for the next start in koan.workspaces.list and
//   koan.folders.list).
// The C++ side is koan_import_browsertest.cc; this checks the pages.
//
//   scripts/e2e/run.sh import_sources      (scripts/mac/suite.sh on the Mac)
import fs from 'node:fs';
import { CDP_PORT, IS_MAC, browserCall, check, evalIn, finish, pages, readPref, sleep, waitFor } from './lib/koan.mjs';
import { ARC_PINNED, CHROME_BOOKMARK, ZEN_BOOKMARK, ZEN_FOLDER_TAB, ZEN_PINNED, importRoot, statusFile }
  from './lib/import_fixtures.mjs';
import { skipped } from './lib/mac.mjs';

// The fixtures are written before Koan starts (setup/import_sources.mjs), so
// the first list Koan builds, the welcome's, has them: the welcome asks for
// its data as it loads, before this script runs.
let fixtures = null;
try {
  fixtures = JSON.parse(fs.readFileSync(statusFile(importRoot(process.env.KOAN_PROFILE ?? '')), 'utf8'));
} catch {}
check('The fixture profiles were written before Koan started (setup/import_sources.mjs)', !!fixtures,
      process.env.KOAN_PROFILE ?? 'no KOAN_PROFILE');
if (!fixtures) finish();
const { zen: zenOk, chromeHistory: chromeHistoryOk, chromeLogin: chromeLoginOk } = fixtures;

// A page's text with every shadow root's (WebUI pages are custom elements).
const DEEP = `(() => {
  const deep = (root, out) => {
    for (const el of root.querySelectorAll('*')) {
      if (el.shadowRoot) deep(el.shadowRoot, out);
    }
    out.push(root.textContent ?? '');
    return out;
  };
  window.__deepText = () => deep(document, []).join(' ');
  window.__deepQuery = selector => {
    const find = root => {
      const hit = root.querySelector(selector);
      if (hit) return hit;
      for (const el of root.querySelectorAll('*')) {
        const inner = el.shadowRoot && find(el.shadowRoot);
        if (inner) return inner;
      }
      return null;
    };
    return find(document);
  };
  return true;
})()`;

async function open(url) {
  const { targetId } = (await browserCall('Target.createTarget', { url })).result;
  const page = await waitFor(async () => (await pages()).find(p => p.id === targetId), 8000);
  await sleep(1500);
  await evalIn(page, DEEP);
  return page;
}

// --- The welcome's import step.
const welcome = await waitFor(async () => (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
    .find(t => t.url.startsWith('chrome://koan-welcome')), 15000);
check('A first run shows the welcome', !!welcome);
if (!welcome) finish();
const js = expression => evalIn(welcome, expression);
await sleep(1000);
const step = () => js(`document.getElementById('welcome-card')?.getAttribute('step')`);
for (let i = 0; i < 4 && await step() !== 'import'; ++i) {
  await js(`document.querySelector('#welcome-card .step-foot .button.primary')?.click()`);
  await sleep(600);
}
check('The welcome reaches its import step', await step() === 'import', await step());
const choices = await js(`[...document.querySelectorAll('input[name=import]')].map(input => {
  const label = input.closest('label');
  const img = label.querySelector('.tile img');
  return { value: input.value, title: label.querySelector('.choice-title')?.textContent,
           icon: img ? img.src.slice(0, 22) : label.querySelector('.tile')?.textContent ?? null };
})`) ?? [];
console.log(`import step: ${JSON.stringify(choices)}`);
const titles = choices.map(c => c.title);
const indexOf = name => titles.indexOf(name);
check('...lists Zen, Arc and Google Chrome',
      ['Zen', 'Arc', 'Google Chrome'].every(name => titles.includes(name)), titles.join(', '));
const upstream = ['Safari', 'Firefox', 'Bookmarks HTML File'].map(indexOf).filter(i => i >= 0);
check('...before Chromium\'s own sources',
      upstream.every(i => i > Math.max(indexOf('Zen'), indexOf('Arc'), indexOf('Google Chrome'))),
      titles.join(', '));
check('...each with its app icon or initial',
      ['Zen', 'Arc', 'Google Chrome'].every(name => {
        const icon = choices.find(c => c.title === name)?.icon;
        return icon === 'data:image/png;base64,' || icon === name[0];
      }), JSON.stringify(choices));
if (IS_MAC && indexOf('Safari') >= 0) {
  // Safari is always installed on a Mac: its icon is the app's.
  check('...Safari with its app icon', choices[indexOf('Safari')].icon === 'data:image/png;base64,',
        choices[indexOf('Safari')].icon);
}

// "Spaces and pinned tabs" shows for the browsers that have them.
const pick = async name => {
  await js(`document.querySelector('input[name=import][value="${choices[indexOf(name)]?.value}"]')?.click()`);
  await sleep(300);
  return js(`(() => {
    const option = document.querySelector('input[name=import-spaces]')?.closest('label');
    return option ? !option.hidden : null;
  })()`);
};
check('..."Spaces and pinned tabs" for Zen', await pick('Zen') === true);
check('...and for Arc', await pick('Arc') === true);
check('...not for Google Chrome', await pick('Google Chrome') === false);

// Pick Google Chrome, then finish the setup: the import dialog opens on it.
const next = () => js(`document.querySelector('#welcome-card .step-foot .button.primary')?.click()`);
for (let i = 0; i < 3 && await step() !== 'done'; ++i) {
  await next();
  await sleep(700);
}
check('...then the last step', await step() === 'done', await step());
// "Start browsing" closes the page: nothing is asked of it after this.
await next();
const settings = await waitFor(async () => (await pages()).find(p => p.url.startsWith('chrome://settings/importData')), 8000);
check('"Start browsing" opens the import dialog', !!settings);
let dialog = settings;
if (!dialog) {
  dialog = await open('chrome://settings/importData');
} else {
  await sleep(1500);
  await evalIn(dialog, DEEP);
}
const selected = await waitFor(() => evalIn(dialog,
    `window.__deepQuery('#browserSelect')?.selectedOptions?.[0]?.textContent?.trim()`), 6000);
check('...on Google Chrome, the welcome\'s pick', selected === 'Google Chrome', selected);
// The dialog's "Spaces and pinned tabs" (hook in import_data_dialog.html.ts):
// hidden for Chrome, shown when Arc is picked.
const spacesBox = () => evalIn(dialog, `(() => {
  const box = window.__deepQuery('#koanImportDialogSpaces');
  return box ? { hidden: box.hidden, label: box.label } : null;
})()`);
const chromeBox = await spacesBox();
check('...its "Spaces and pinned tabs" is hidden for Chrome',
      chromeBox?.hidden === true && chromeBox?.label === 'Spaces and pinned tabs', JSON.stringify(chromeBox));

// --- Importing, as the dialog's Import button (ImportDataHandler).
const profiles = await evalIn(dialog, `import('chrome://resources/js/cr.js')
    .then(cr => cr.sendWithPromise('initializeImportDialog'))`) ?? [];
async function importFrom(name, { history, bookmarks, passwords, spaces = false }) {
  const index = profiles.find(p => p.name === name)?.index;
  if (index === undefined) return 'not listed';
  await evalIn(dialog, `import('chrome://resources/js/cr.js').then(cr => {
    window.__importStatus = 'pending';
    cr.addWebUiListener('import-data-status-changed', status => {
      if (status !== 'inProgress') window.__importStatus = status;
    });
    chrome.send('importData', [${index}, {
      import_dialog_history: ${history}, import_dialog_bookmarks: ${bookmarks},
      import_dialog_saved_passwords: ${passwords}, import_dialog_search_engine: false,
      import_dialog_autofill_form_data: false, 'koan.import_dialog.spaces': ${spaces}}]);
    return true;
  })`);
  return waitFor(async () => {
    const status = await evalIn(dialog, 'window.__importStatus');
    return status !== 'pending' && status;
  }, 30000);
}
const pickInDialog = name => evalIn(dialog, `(() => {
  const select = window.__deepQuery('#browserSelect');
  const option = [...select.options].find(o => o.textContent.trim() === ${JSON.stringify(name)});
  if (!option) return false;
  select.selectedIndex = option.index;
  select.dispatchEvent(new Event('change'));
  return true;
})()`);
if (await pickInDialog('Arc')) {
  await sleep(500);
  check('...and shown for Arc', (await spacesBox())?.hidden === false, JSON.stringify(await spacesBox()));
} else {
  check('...and shown for Arc', false, 'Arc not in the dialog');
}
check('The dialog offers Spaces for Zen and Arc, not for Chrome',
      profiles.find(p => p.name === 'Zen')?.koanSpaces === true &&
      profiles.find(p => p.name === 'Arc')?.koanSpaces === true &&
      !profiles.find(p => p.name === 'Google Chrome')?.koanSpaces,
      JSON.stringify(profiles.map(p => [p.name, p.koanSpaces])));
const chromeEntry = profiles.find(p => p.name === 'Google Chrome');
check('The dialog offers Chrome\'s history, bookmarks and passwords',
      chromeEntry?.history && chromeEntry?.favorites && chromeEntry?.passwords, JSON.stringify(chromeEntry));
check('Importing from Chrome succeeds',
      await importFrom('Google Chrome', { history: true, bookmarks: true, passwords: true }) === 'succeeded');
if (zenOk) {
  const zenEntry = profiles.find(p => p.name === 'Zen');
  check('Zen offers history and bookmarks, no passwords (Chromium\'s Firefox importer on macOS)',
        zenEntry?.history && zenEntry?.favorites && !zenEntry?.passwords, JSON.stringify(zenEntry));
  check('Importing from Zen (with its Spaces) succeeds',
        await importFrom('Zen', { history: true, bookmarks: true, passwords: false, spaces: true }) === 'succeeded');
} else {
  skipped('the Zen import', 'no node:sqlite or python3 to write its places.sqlite');
}

check('Importing Arc\'s Spaces and pinned tabs alone succeeds',
      await importFrom('Arc', { history: false, bookmarks: false, passwords: false, spaces: true }) === 'succeeded');

// --- What landed.
// The Spaces, kept for a start that restores no session (their tabs never
// loaded). (Zen's first took the window's untouched Space; Arc's came after.)
const storedSpaces = async () => (await readPref('koan.workspaces.list')) ?? [];
const spacesLanded = await waitFor(async () => {
  const list = await storedSpaces();
  const titles = list.map(space => space.title);
  return titles.includes('Reading') && (!zenOk || titles.includes('Zen Work')) && list;
}, 15000);
const spaceTitles = (spacesLanded || []).map(space => space.title);
check('Arc\'s and Zen\'s Spaces are Koan Spaces', !!spacesLanded, JSON.stringify(spaceTitles));
const pinnedOf = title => (spacesLanded || []).find(space => space.title === title)?.pinned?.map(tab => tab.url) ?? [];
check('...with their pinned tabs (unloaded)', pinnedOf('Reading').includes(ARC_PINNED) &&
      (!zenOk || pinnedOf('Zen Work').includes(ZEN_PINNED)), JSON.stringify(spacesLanded));
if (zenOk) {
  const folders = (await readPref('koan.folders.list')) ?? [];
  check('...and Zen\'s folder with its tab',
        folders.some(folder => folder.name === 'Zen folder' && folder.tabs?.some(tab => tab.url === ZEN_FOLDER_TAB)),
        JSON.stringify(folders));
}
const bookmarksPage = await open('chrome://bookmarks/');
const bookmarked = url => evalIn(bookmarksPage,
    `new Promise(r => chrome.bookmarks.search({url: ${JSON.stringify(url)}}, r)).then(found => found.length > 0)`);
check('Chrome\'s bookmark is in the bookmarks', await waitFor(() => bookmarked(CHROME_BOOKMARK), 5000));
if (zenOk) {
  check('Zen\'s bookmark is in the bookmarks', await waitFor(() => bookmarked(ZEN_BOOKMARK), 5000));
}
const inPage = async (url, text) => {
  const page = await open(url);
  return waitFor(async () => {
    await evalIn(page, DEEP);
    return evalIn(page, `window.__deepText().includes(${JSON.stringify(text)})`);
  }, 8000);
};
if (chromeHistoryOk) {
  check('Chrome\'s history is in the history', await inPage('chrome://history/', 'chrome-history.example'));
}
if (zenOk) {
  check('Zen\'s history is in the history', await inPage('chrome://history/', 'zen-history.example'));
}
if (chromeLoginOk) {
  check('Chrome\'s saved password is in Passwords',
        await inPage('chrome://password-manager/passwords', 'chrome-login.example'));
}
if (!chromeHistoryOk || !chromeLoginOk) {
  skipped('Chrome\'s history and password', 'no node:sqlite or python3 to write its files');
}
finish();
