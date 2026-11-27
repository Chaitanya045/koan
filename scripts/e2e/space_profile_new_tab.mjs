// End-to-end test of #543: a tab opened at once after a Space gets a new
// profile is in that profile, never in the default partition.
// - form: the Create-a-Space form (Ctrl+Alt+N, a name, Profile > New
//   Profile…, a profile name, Create, Create Space), then at once a new tab.
// - set: Set Profile > New Profile… in the menu of a Space that has none
//   (right-click its header), then at once a new tab.
// The new tab is CDP's Target.createTarget, which opens like Cmd+T (in the
// active Space, with no page behind it), or with OPEN=urlbar what the user
// does: Ctrl+T, the address in the URL bar, Enter.
// The first Space (no profile) sets a cookie and a localStorage value in the
// default partition, and each round's tab sets its own: every new tab must
// see none of them (nor the server get a cookie), as a fresh profile's.
// ROUNDS (default 20) rounds of each of PATHS (default form,set), with no
// wait between the last click and the new tab.
// Run with scripts/e2e/run.sh space_profile_new_tab.
import { CTRL, ALT, browserCall, check, evalIn, finish, openMenus, pages, readPref,
         sleep, typeInto, viewMouse, viewTree, waitFor } from './lib/koan.mjs';
import { fatalInLog, key, rootMouse, spaceButtons, startServer } from './lib/mac.mjs';

const ROUNDS = Number(process.env.ROUNDS || 20);
const PATHS = (process.env.PATHS || 'form,set').split(',');

const server = await startServer({
  '/probe': `<!doctype html><title>Probe</title><script>
    window.state = async () => JSON.stringify({
      cookie: document.cookie, storage: localStorage.getItem('where'),
      server: await (await fetch('/whoami', { cache: 'no-store' })).text() });
  </script>`,
  '/whoami': req => req.headers.cookie ?? '',
});
const PROBE = tag => server.url(`/probe?${encodeURIComponent(tag)}`);
const state = async page => JSON.parse(await evalIn(page, 'state()') ?? '{}');
const inDefault = s => /where=default/.test(s.cookie ?? '') || s.storage === 'default' ||
    /where=default/.test(s.server ?? '');
// A fresh profile's: no mark of the default partition's or an earlier round's.
const fresh = s => !s.cookie && s.storage === null && !s.server;
const mark = (page, tag) => evalIn(page, `document.cookie = 'where=${tag}; max-age=86400; path=/'; ` +
                                         `localStorage.setItem('where', '${tag}'); true`);

const shown = (vs, cls) => vs.filter(v => v.cls === cls && v.shown);
const click = async view => {
  await viewMouse(view, 'mousePressed', Math.round(view.w / 2), Math.round(view.h / 2));
  await viewMouse(view, 'mouseReleased', Math.round(view.w / 2), Math.round(view.h / 2));
};
const buttons = async () => spaceButtons(await viewTree());

// The tab shown (a page closed meanwhile, as a replaced empty tab, is
// skipped).
async function shownPage() {
  for (const page of await pages()) {
    if (await evalIn(page, 'document.visibilityState').catch(() => '') === 'visible') return page;
  }
  return null;
}

// Opens a probe page in a new tab of the active Space at once; its page.
// OPEN=urlbar: as the user does, Ctrl+T (the URL bar in place of a new tab),
// the address, Enter; else CDP's Target.createTarget.
async function openProbe(tag) {
  if (process.env.OPEN === 'urlbar' && tag !== 'home') {
    await key(await shownPage() ?? home, 't', CTRL);
    const omnibox = await waitFor(async () => (await viewTree()).find(
        v => v.cls === 'OmniboxViewViews' && v.shown), 5000);
    if (!omnibox) return null;
    const before = new Set((await pages()).map(p => p.id));
    await typeInto(omnibox, PROBE(tag), 13);
    const page = await waitFor(async () => (await pages()).find(
        p => p.url === PROBE(tag)), 8000);
    if (!page) {
      const added = (await pages()).filter(p => !before.has(p.id)).map(p => p.url);
      console.log(`  (${tag}: no tab at ${PROBE(tag)}; new: ${JSON.stringify(added)}, omnibox ` +
                  `${JSON.stringify((await viewTree()).find(v => v.cls === 'OmniboxViewViews')?.props.Text)})`);
    }
    if (page) await waitFor(() => evalIn(page, 'typeof window.state === "function"'), 8000);
    return page;
  }
  const targetId = (await browserCall('Target.createTarget', { url: PROBE(tag) })).result?.targetId;
  const page = await waitFor(async () => (await pages()).find(p => p.id === targetId), 8000);
  if (page) await waitFor(() => evalIn(page, 'typeof window.state === "function"'), 8000);
  return page;
}

// Picks "New Profile…" (the last item: MenuItemView paints its title, so
// UI DevTools can't read it) in the open profile menu and names the profile.
async function newProfileFromMenu(name, parent = null) {
  const menu = await waitFor(async () => openMenus(await viewTree()).find(
      m => m.root?.widget !== parent?.root?.widget), 5000);
  if (!menu) return 'no profile menu';
  // (The menu grows by one profile a round: space_profile_new_tab.flags
  // gives the headless screen the height to show it whole.)
  // At the item's far end: a submenu overlaps its parent at its start.
  const last = menu.items.at(-1);
  // (rootMouse(): the menu's root as the clicking session numbers it.)
  await rootMouse(menu.root, ['mouseMoved'], last.ax + last.w - 20, last.ay + last.h / 2, 'none');
  await rootMouse(menu.root, ['mousePressed', 'mouseReleased'], last.ax + last.w - 20,
                  last.ay + last.h / 2);
  const field = await waitFor(async () => (await viewTree()).find(
      v => v.cls === 'Textfield' && v.shown && v.path.includes('BubbleDialogModelHostContentsView')),
      5000);
  if (!field) return 'no New Profile dialog';
  // The name must arrive: a window that comes or goes (a toast) renumbers the
  // nodes, and keys sent to the old number are lost; Create with no name then
  // closes the dialog without a profile.
  const dialogField = async () => (await viewTree()).find(
      v => v.cls === 'Textfield' && v.shown && v.path.includes('BubbleDialogModelHostContentsView'));
  for (let tries = 0; tries < 3 && (await dialogField())?.props.Text !== name; tries++) {
    const now = await dialogField();
    if (now?.props.Text) break;
    await typeInto(now, name);
  }
  if ((await dialogField())?.props.Text !== name) return 'the profile name did not reach the dialog';
  const create = shown(await viewTree(), 'MdTextButton').find(
      v => v.props.Text === 'Create' && v.path.includes('DialogClientView'));
  if (!create) return 'no Create button in the New Profile dialog';
  await click(create);
  return '';
}

// The first Space (no profile) sets its marks in the default partition.
const home = await openProbe('home');
await mark(home, 'default');
let s = await state(home);
check('The first Space sets a cookie and a localStorage value', inDefault(s), JSON.stringify(s));

// Reads the round's new tab: a failure, or '' (and marks it for the next).
async function judge(i, page, name) {
  if (!page) return 'no tab';
  s = await state(page);
  const active = (await buttons()).find(b => b.active)?.name;
  await mark(page, `round-${i}`);
  if (inDefault(s)) return `in the default partition (Space ${active}): ${JSON.stringify(s)}`;
  if (!fresh(s)) return `not in a fresh profile (Space ${active}): ${JSON.stringify(s)}`;
  if (active !== name) return `the active Space is ${active}, not ${name}`;
  return '';
}

const results = {};
// --- The Create-a-Space form with a new profile.
if (PATHS.includes('form')) {
  let bad = 0, done = 0, unset = 0;
  const failures = [];
  for (let i = 1; i <= ROUNDS; i++) {
    const name = `Work ${i}`;
    await key(home, 'n', CTRL | ALT, 800);
    const nameField = await waitFor(async () => (await viewTree()).find(
        v => v.cls === 'Textfield' && v.shown && v.path.includes('KoanSpaceCreationView')), 5000);
    if (!nameField) { failures.push(`${i}: no form`); continue; }
    await typeInto(nameField, name);
    const profileButton = shown(await viewTree(), 'KoanSpaceProfileButton')[0];
    await click(profileButton);
    const why = await newProfileFromMenu(name);
    if (why) { failures.push(`${i}: ${why}`); continue; }
    await sleep(600);
    const create = shown(await viewTree(), 'MdTextButton').find(
        v => v.props.Text === 'Create Space' && v.path.includes('KoanSpaceCreationView'));
    if (!create) { failures.push(`${i}: no Create Space button`); continue; }
    const picked = shown(await viewTree(), 'KoanSpaceProfileButton')[0]?.props.Text;
    if (picked !== name) {
      // Why: was the profile made, is the dialog still up?
      const saved = (await readPref('koan.containers.user'))?.filter?.(c => c.name === name) ?? [];
      const dialog = shown(await viewTree(), 'MdTextButton').some(
          v => v.props.Text === 'Create' && v.path.includes('DialogClientView'));
      failures.push(`${i}: the form's profile is ${picked} (saved as ${JSON.stringify(saved)}, ` +
                    `dialog ${dialog ? 'still open' : 'closed'})`);
      unset++;
    }
    // Create Space, then the new tab with no wait.
    await click(create);
    const why2 = await judge(i, await openProbe(`form-${i}`), name);
    done++;
    if (why2) {
      bad++;
      failures.push(`${i}: ${why2}`);
    }
  }
  results.form = { done, bad, unset, failures };
  console.log(`form: ${done} rounds, ${bad} not in their profile, ${unset} with the form on another profile`);
  for (const f of failures) console.log(`  ${f}`);
  check(`Create a Space with New Profile…, then a tab at once: ${ROUNDS} rounds, always in the new profile`,
        done === ROUNDS && bad === 0 && unset === 0, `${done} rounds; ${failures.join('; ')}`);
}

// --- Set Profile > New Profile… on an existing Space (the header's menu).
if (PATHS.includes('set')) {
  let bad = 0, done = 0;
  const failures = [];
  for (let i = 1; i <= ROUNDS; i++) {
    const name = `Set ${i}`;
    // A Space with no profile first; its empty tab settles.
    await key(home, 'n', CTRL | ALT, 800);
    const nameField = await waitFor(async () => (await viewTree()).find(
        v => v.cls === 'Textfield' && v.shown && v.path.includes('KoanSpaceCreationView')), 5000);
    if (!nameField) { failures.push(`${i}: no form`); continue; }
    await typeInto(nameField, name, 13);
    if (!await waitFor(async () => (await buttons()).find(b => b.active)?.name === name, 5000)) {
      failures.push(`${i}: the Space wasn't made`);
      continue;
    }
    const header = shown(await viewTree(), 'KoanSpaceHeaderView')[0];
    await viewMouse(header, 'mousePressed', 20, 20, 'right');
    await viewMouse(header, 'mouseReleased', 20, 20, 'right');
    const menu = await waitFor(async () => openMenus(await viewTree())[0], 5000);
    // Change Name…, Change Icon…, Theme…, Set Profile.
    if (!menu || menu.items.length < 4) { failures.push(`${i}: no Space menu`); continue; }
    await rootMouse(menu.root, ['mouseMoved'], menu.items[3].ax + 8,
                    menu.items[3].ay + menu.items[3].h / 2, 'none');
    if (!await waitFor(async () => openMenus(await viewTree()).length > 1, 3000) &&
        (await rootMouse(menu.root, ['mousePressed', 'mouseReleased'], menu.items[3].ax + 8,
                         menu.items[3].ay + menu.items[3].h / 2),
         !await waitFor(async () => openMenus(await viewTree()).length > 1, 3000))) {
      failures.push(`${i}: no Set Profile submenu`);
      continue;
    }
    const why = await newProfileFromMenu(name, menu);
    if (why) { failures.push(`${i}: ${why}`); continue; }
    // (The dialog's Create sets the profile; the new tab follows at once.)
    const why2 = await judge(i, await openProbe(`set-${i}`), name);
    done++;
    if (why2) {
      bad++;
      failures.push(`${i}: ${why2}`);
    }
  }
  results.set = { done, bad, failures };
  console.log(`set: ${done} rounds, ${bad} not in their profile`);
  for (const f of failures) console.log(`  ${f}`);
  check(`Set Profile > New Profile…, then a tab at once: ${ROUNDS} rounds, always in the new profile`,
        done === ROUNDS && bad === 0, `${done} rounds; ${failures.join('; ')}`);
}

console.log(JSON.stringify(results));
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
server.close();
finish();
