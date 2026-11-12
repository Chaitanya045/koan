// The fixture profiles of import_sources.mjs (#348, #363): a Zen (with its
// Spaces in zen-sessions.jsonlz4), a Chrome and an Arc profile (its Spaces
// in StorableSidebar.json), written under the test root that
// import_sources.flags passes
// (--koan-import-test-root={profile}/import-root) by
// scripts/e2e/setup/import_sources.mjs, before Koan starts, so the first
// list Koan builds (the welcome's) already has them. Never the user's own.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

export const ZEN_BOOKMARK = 'https://zen-bookmark.example/';
export const ZEN_HISTORY = 'https://zen-history.example/';
export const CHROME_BOOKMARK = 'https://chrome-bookmark.example/';
export const CHROME_HISTORY = 'https://chrome-history.example/';
export const CHROME_LOGIN = 'https://chrome-login.example/';
export const ARC_PINNED = 'https://arc-pinned.example/';
export const ARC_FAVORITE = 'https://arc-favorite.example/';
export const ZEN_PINNED = 'https://zen-pinned.example/';
export const ZEN_ESSENTIAL = 'https://zen-essential.example/';
export const ZEN_FOLDER_TAB = 'https://zen-folder.example/';
// "hunter2" under the key of the Safe Storage secret "peanuts" (os_crypt's
// macOS scheme; koan_chromium_profile_reader_unittest.cc).
const HUNTER2 = '7218b8ec8774bcaad0e8305f7a6cf965';

// The test root of a run's profile, and the setup's record of what it wrote.
export const importRoot = profile => path.join(profile, 'import-root');
export const statusFile = root => path.join(root, 'fixtures.json');

function write(file, contents) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, contents);
}

// `json` as Firefox's mozLz4 (.jsonlz4): the magic, the size and an LZ4 block
// of literals only (15 or more: the rest of the length in bytes of 255 and
// one less), which any LZ4 decoder reads.
function mozLz4(json) {
  const data = Buffer.from(json, 'utf8');
  const head = Buffer.alloc(12);
  head.write('mozLz40\0', 0, 'latin1');
  head.writeUInt32LE(data.length, 8);
  const length = [0xf0];
  let rest = data.length - 15;
  for (; rest >= 255; rest -= 255) length.push(255);
  length.push(rest);
  return Buffer.concat([head, Buffer.from(length), data]);
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

// Writes the profiles under `root`; returns which SQLite files it could
// write ({zen, chromeHistory, chromeLogin}), also kept in statusFile(root).
export async function writeImportFixtures(root) {
  const ROOT = root;
  const nowFirefox = BigInt(Date.now()) * 1000n;
  const nowChromium = (BigInt(Date.now()) + 11644473600000n) * 1000n;

  // --- The fixture profiles.
  const zenProfile = path.join(ROOT, 'zen', 'Profiles', 'e2e.Default (release)');
  write(path.join(ROOT, 'zen', 'profiles.ini'),
        '[Profile0]\nName=Default (release)\nIsRelative=1\n' +
        'Path=Profiles/e2e.Default (release)\nDefault=1\n');
  const zen = await sqlite(path.join(zenProfile, 'places.sqlite'), `
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
  // Zen's Spaces as Zen 1.24 keeps them (ZenSessionManager): two Spaces, an
  // Essential, a pinned tab and a folder with a tab.
  write(path.join(zenProfile, 'zen-sessions.jsonlz4'), mozLz4(JSON.stringify({
    spaces: [
      { uuid: '{e2e-work}', name: 'Zen Work',
        theme: { type: 'gradient', gradientColors: [], opacity: 0.5, texture: 0 } },
      { uuid: '{e2e-play}', name: 'Zen Play' }],
    folders: [{ id: 'zf', name: 'Zen folder', collapsed: false, parentId: null,
                workspaceId: '{e2e-work}' }],
    tabs: [
      { pinned: true, zenEssential: true, zenWorkspace: '{e2e-work}', index: 1,
        entries: [{ url: ZEN_ESSENTIAL, title: 'Zen essential' }] },
      { pinned: true, zenWorkspace: '{e2e-work}', index: 1,
        entries: [{ url: ZEN_PINNED, title: 'Zen pinned' }] },
      { pinned: true, zenWorkspace: '{e2e-work}', groupId: 'zf', index: 1,
        entries: [{ url: ZEN_FOLDER_TAB, title: 'In a folder' }] }],
  })));

  const chrome = path.join(ROOT, 'Google', 'Chrome');
  write(path.join(chrome, 'Local State'), JSON.stringify(
      { profile: { last_used: 'Default', info_cache: { Default: { name: 'Person 1' } } } }));
  write(path.join(chrome, 'Default', 'Bookmarks'), JSON.stringify({ version: 1, roots: {
    bookmark_bar: { type: 'folder', name: 'Bookmarks bar', children: [
      { type: 'url', name: 'Chrome bookmark', url: CHROME_BOOKMARK }] } } }));
  const chromeHistory = await sqlite(path.join(chrome, 'Default', 'History'), `
    CREATE TABLE urls(id INTEGER PRIMARY KEY AUTOINCREMENT, url LONGVARCHAR, title LONGVARCHAR,
      visit_count INTEGER DEFAULT 0 NOT NULL, typed_count INTEGER DEFAULT 0 NOT NULL,
      last_visit_time INTEGER NOT NULL, hidden INTEGER DEFAULT 0 NOT NULL);
    INSERT INTO urls (url, title, visit_count, typed_count, last_visit_time)
      VALUES ('${CHROME_HISTORY}', 'Chrome history', 3, 1, ${nowChromium});
  `);
  const chromeLogin = await sqlite(path.join(chrome, 'Default', 'Login Data'), `
    CREATE TABLE logins (origin_url VARCHAR NOT NULL, action_url VARCHAR,
      username_element VARCHAR, username_value VARCHAR, password_element VARCHAR,
      password_value BLOB, signon_realm VARCHAR NOT NULL,
      blacklisted_by_user INTEGER NOT NULL, scheme INTEGER NOT NULL);
    INSERT INTO logins VALUES ('${CHROME_LOGIN}', '${CHROME_LOGIN}', 'user', 'ana', 'pass',
      X'763130${HUNTER2}', '${CHROME_LOGIN}', 0, 0);
  `);
  write(path.join(ROOT, 'Keychain', 'Chrome Safe Storage'), 'peanuts');
  // Arc: a profile, its Spaces, pinned tabs and Favorites beside "User Data".
  write(path.join(ROOT, 'Arc', 'User Data', 'Default', 'Bookmarks'),
        JSON.stringify({ version: 1, roots: {} }));
  write(path.join(ROOT, 'Arc', 'StorableSidebar.json'), JSON.stringify({ sidebar: { containers: [
    { global: {} }, {
      topAppsContainerIDs: [{ default: true }, 'top'],
      spaces: ['s', { id: 's', title: 'Reading', profile: { default: true },
                      containerIDs: ['pinned', 'p'] }],
      items: ['top', { id: 'top', childrenIds: ['fav'] },
              'fav', { id: 'fav', title: 'Favorite', data: { tab: { savedURL: ARC_FAVORITE } } },
              'p', { id: 'p', childrenIds: ['t'] },
              't', { id: 't', title: 'Pinned', data: { tab: { savedURL: ARC_PINNED } } }],
    }] } }));
  const status = { zen, chromeHistory, chromeLogin };
  write(statusFile(root), JSON.stringify(status));
  return status;
}
