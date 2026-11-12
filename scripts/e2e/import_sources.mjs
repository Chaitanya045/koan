// End-to-end test of the import from Zen, Arc and Chrome (#348), with
// fixture profiles written under the test root (import_sources.flags:
// --koan-import-test-root={profile}/import-root), never the user's own:
// - the welcome's import step lists Zen, Arc and Google Chrome, before
//   Chromium's own sources, each with its app icon (or its initial when the
//   app isn't installed);
// - picking Google Chrome there opens Chromium's import dialog on it;
// - importing from Chrome brings its bookmarks, history and saved password
//   (its Safe Storage key from the test root, never the keychain), and from
//   Zen (Chromium's Firefox importer) its bookmarks and history.
// The C++ side is koan_import_browsertest.cc; this checks the pages.
//
//   scripts/e2e/run.sh import_sources      (scripts/mac/suite.sh on the Mac)
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { CDP_PORT, IS_MAC, browserCall, check, evalIn, finish, pages, sleep, waitFor } from './lib/koan.mjs';
import { skipped } from './lib/mac.mjs';

const ROOT = path.join(process.env.KOAN_PROFILE ?? '', 'import-root');
const ZEN_BOOKMARK = 'https://zen-bookmark.example/';
const ZEN_HISTORY = 'https://zen-history.example/';
const CHROME_BOOKMARK = 'https://chrome-bookmark.example/';
const CHROME_HISTORY = 'https://chrome-history.example/';
const CHROME_LOGIN = 'https://chrome-login.example/';
// "hunter2" under the key of the Safe Storage secret "peanuts" (os_crypt's
// macOS scheme; koan_chromium_profile_reader_unittest.cc).
const HUNTER2 = '7218b8ec8774bcaad0e8305f7a6cf965';

if (!process.env.KOAN_PROFILE) {
  check('KOAN_PROFILE is set (the suite sets it)', false);
  finish();
}

function write(file, contents) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, contents);
}

// Runs SQL into a new SQLite file: node:sqlite where Node has it, else
// Python's sqlite3. False if neither is there.
async function sqlite(file, sql) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  try {
    const { DatabaseSync } = await import('node:sqlite');
    const db = new DatabaseSync(file);
    db.exec(sql);
    db.close();
    return true;
  } catch {}
  const python = spawnSync('python3', ['-I', '-c',
      'import sqlite3, sys\nc = sqlite3.connect(sys.argv[1])\n' +
      'c.executescript(sys.stdin.read())\nc.commit()', file], { input: sql });
  return python.status === 0;
}

const nowFirefox = BigInt(Date.now()) * 1000n;
const nowChromium = (BigInt(Date.now()) + 11644473600000n) * 1000n;

// --- The fixture profiles.
const zenProfile = path.join(ROOT, 'zen', 'Profiles', 'e2e.Default (release)');
write(path.join(ROOT, 'zen', 'profiles.ini'),
      '[Profile0]\nName=Default (release)\nIsRelative=1\n' +
      'Path=Profiles/e2e.Default (release)\nDefault=1\n');
const zenOk = await sqlite(path.join(zenProfile, 'places.sqlite'), `
  CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR,
    rev_host LONGVARCHAR, visit_count INTEGER DEFAULT 0, hidden INTEGER DEFAULT 0 NOT NULL,
    typed INTEGER DEFAULT 0 NOT NULL, frecency INTEGER DEFAULT -1 NOT NULL,
    last_visit_date INTEGER, guid TEXT, foreign_count INTEGER DEFAULT 0 NOT NULL,
    url_hash INTEGER DEFAULT 0 NOT NULL);
  CREATE TABLE moz_historyvisits (id INTEGER PRIMARY KEY, from_visit INTEGER,
    place_id INTEGER, visit_date INTEGER, visit_type INTEGER, session INTEGER,
    source INTEGER DEFAULT 0 NOT NULL, triggeringPlaceId INTEGER);
  CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, type INTEGER, fk INTEGER DEFAULT NULL,
    parent INTEGER, position INTEGER, title LONGVARCHAR, keyword_id INTEGER,
    folder_type TEXT, dateAdded INTEGER, lastModified INTEGER, guid TEXT,
    syncStatus INTEGER NOT NULL DEFAULT 0, syncChangeCounter INTEGER NOT NULL DEFAULT 1);
  CREATE TABLE moz_keywords (id INTEGER PRIMARY KEY AUTOINCREMENT, keyword TEXT UNIQUE,
    place_id INTEGER, post_data TEXT);
  CREATE TABLE moz_anno_attributes (id INTEGER PRIMARY KEY, name VARCHAR(32) UNIQUE NOT NULL);
  CREATE TABLE moz_items_annos (id INTEGER PRIMARY KEY, item_id INTEGER NOT NULL,
    anno_attribute_id INTEGER, content LONGVARCHAR, flags INTEGER DEFAULT 0,
    expiration INTEGER DEFAULT 0, type INTEGER DEFAULT 0, dateAdded INTEGER DEFAULT 0,
    lastModified INTEGER DEFAULT 0);
  INSERT INTO moz_places (id, url, title, visit_count, typed, guid) VALUES
    (1, '${ZEN_HISTORY}', 'Zen history', 2, 1, 'p1'),
    (2, '${ZEN_BOOKMARK}', 'Zen bookmark', 1, 0, 'p2');
  INSERT INTO moz_historyvisits (place_id, visit_date, visit_type) VALUES
    (1, ${nowFirefox}, 1), (2, ${nowFirefox}, 1);
  INSERT INTO moz_bookmarks (id, type, fk, parent, position, title, dateAdded, guid) VALUES
    (1, 2, NULL, 0, 0, '', ${nowFirefox}, 'root________'),
    (2, 2, NULL, 1, 0, 'menu', ${nowFirefox}, 'menu________'),
    (3, 2, NULL, 1, 1, 'toolbar', ${nowFirefox}, 'toolbar_____'),
    (5, 2, NULL, 1, 3, 'unfiled', ${nowFirefox}, 'unfiled_____'),
    (10, 1, 2, 3, 0, 'Zen bookmark', ${nowFirefox}, 'b10');
`);

const chrome = path.join(ROOT, 'Google', 'Chrome');
write(path.join(chrome, 'Local State'), JSON.stringify(
    { profile: { last_used: 'Default', info_cache: { Default: { name: 'Person 1' } } } }));
write(path.join(chrome, 'Default', 'Bookmarks'), JSON.stringify({ version: 1, roots: {
  bookmark_bar: { type: 'folder', name: 'Bookmarks bar', children: [
    { type: 'url', name: 'Chrome bookmark', url: CHROME_BOOKMARK }] } } }));
const chromeHistoryOk = await sqlite(path.join(chrome, 'Default', 'History'), `
  CREATE TABLE urls(id INTEGER PRIMARY KEY AUTOINCREMENT, url LONGVARCHAR, title LONGVARCHAR,
    visit_count INTEGER DEFAULT 0 NOT NULL, typed_count INTEGER DEFAULT 0 NOT NULL,
    last_visit_time INTEGER NOT NULL, hidden INTEGER DEFAULT 0 NOT NULL);
  INSERT INTO urls (url, title, visit_count, typed_count, last_visit_time)
    VALUES ('${CHROME_HISTORY}', 'Chrome history', 3, 1, ${nowChromium});
`);
const chromeLoginOk = await sqlite(path.join(chrome, 'Default', 'Login Data'), `
  CREATE TABLE logins (origin_url VARCHAR NOT NULL, action_url VARCHAR,
    username_element VARCHAR, username_value VARCHAR, password_element VARCHAR,
    password_value BLOB, signon_realm VARCHAR NOT NULL,
    blacklisted_by_user INTEGER NOT NULL, scheme INTEGER NOT NULL);
  INSERT INTO logins VALUES ('${CHROME_LOGIN}', '${CHROME_LOGIN}', 'user', 'ana', 'pass',
    X'763130${HUNTER2}', '${CHROME_LOGIN}', 0, 0);
`);
write(path.join(ROOT, 'Keychain', 'Chrome Safe Storage'), 'peanuts');
// Arc: a profile, its pinned tabs beside "User Data".
write(path.join(ROOT, 'Arc', 'User Data', 'Default', 'Bookmarks'),
      JSON.stringify({ version: 1, roots: {} }));
write(path.join(ROOT, 'Arc', 'StorableSidebar.json'), JSON.stringify({ sidebar: { containers: [
  { global: {} }, {
    spaces: ['s', { id: 's', title: 'Reading', profile: { default: true },
                    containerIDs: ['pinned', 'p'] }],
    items: ['p', { id: 'p', childrenIds: ['t'] },
            't', { id: 't', title: 'Pinned', data: { tab: { savedURL: 'https://arc-pinned.example/' } } }],
  }] } }));

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
// Its list was made before the fixtures existed: load it again.
await js('location.reload()');
await sleep(2500);
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

// Pick Google Chrome, then finish the setup: the import dialog opens on it.
await js(`document.querySelector('input[name=import][value="${choices[indexOf('Google Chrome')]?.value}"]')?.click()`);
await sleep(300);
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

// --- Importing, as the dialog's Import button (ImportDataHandler).
const profiles = await evalIn(dialog, `import('chrome://resources/js/cr.js')
    .then(cr => cr.sendWithPromise('initializeImportDialog'))`) ?? [];
async function importFrom(name, { history, bookmarks, passwords }) {
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
      import_dialog_autofill_form_data: false}]);
    return true;
  })`);
  return waitFor(async () => {
    const status = await evalIn(dialog, 'window.__importStatus');
    return status !== 'pending' && status;
  }, 30000);
}
const chromeEntry = profiles.find(p => p.name === 'Google Chrome');
check('The dialog offers Chrome\'s history, bookmarks and passwords',
      chromeEntry?.history && chromeEntry?.favorites && chromeEntry?.passwords, JSON.stringify(chromeEntry));
check('Importing from Chrome succeeds',
      await importFrom('Google Chrome', { history: true, bookmarks: true, passwords: true }) === 'succeeded');
if (zenOk) {
  const zenEntry = profiles.find(p => p.name === 'Zen');
  check('Zen offers history and bookmarks, no passwords (Chromium\'s Firefox importer on macOS)',
        zenEntry?.history && zenEntry?.favorites && !zenEntry?.passwords, JSON.stringify(zenEntry));
  check('Importing from Zen succeeds',
        await importFrom('Zen', { history: true, bookmarks: true, passwords: false }) === 'succeeded');
} else {
  skipped('the Zen import', 'no node:sqlite or python3 to write its places.sqlite');
}

// --- What landed.
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
