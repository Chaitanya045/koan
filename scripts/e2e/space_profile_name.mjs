// End-to-end test of #576: the New Profile dialog never closes without a
// profile. From both of its entry points (the Create-a-Space form's Profile
// row, and Set Profile > New Profile… in a Space's menu):
// - Create is off with the name empty and with only spaces, on once a name
//   is typed;
// - a 65-character name is refused with a message, and the dialog stays
//   open; typing again takes the message away;
// - Create with a good name closes the dialog, and the form's Profile row
//   (or the Space) has the new profile at once.
// Also the recorded walk-through of the PR: with PRTEST_STEPS set (the
// pr-test prtest.sh), each step() is a caption and LOOK_MS (default 0) a
// pause after it.
// Run with scripts/e2e/run.sh space_profile_name.
import fs from 'node:fs';
import { CTRL, ALT, check, finish, openMenus, pages, readPref, sleep, typeInto,
         viewEvent, viewMouse, viewTree, waitFor } from './lib/koan.mjs';
import { fatalInLog, key, rootMouse, spaceButtons } from './lib/mac.mjs';

const LOOK_MS = Number(process.env.LOOK_MS || 0);
function step(text) {
  console.log(`STEP  ${text}`);
  if (process.env.PRTEST_STEPS) {
    fs.appendFileSync(process.env.PRTEST_STEPS, JSON.stringify({ t: Date.now(), text }) + '\n');
  }
}
const look = (ms = LOOK_MS) => sleep(ms);

const shown = (vs, cls) => vs.filter(v => v.cls === cls && v.shown);
const click = async view => {
  await viewMouse(view, 'mousePressed', Math.round(view.w / 2), Math.round(view.h / 2));
  await viewMouse(view, 'mouseReleased', Math.round(view.w / 2), Math.round(view.h / 2));
};
const inDialog = v => v.path.includes('BubbleDialogModelHostContentsView');
// (Read again each time: a window that comes or goes renumbers the nodes.)
const nameField = async () => (await viewTree()).find(
    v => v.cls === 'Textfield' && v.shown && inDialog(v));
const createButton = async () => shown(await viewTree(), 'MdTextButton').find(
    v => v.props.Text === 'Create' && v.path.includes('DialogClientView'));
const createEnabled = async () => (await createButton())?.props.Enabled;
// The dialog's message saying `re`, if it shows.
const message = async re => (await viewTree()).find(
    v => v.shown && inDialog(v) && re.test(v.props.Text ?? ''));
const TOO_LONG = /too long/;

async function clearName() {
  const field = await nameField();
  for (let i = (field?.props.Text ?? '').length; i > 0; i--) {
    await viewEvent(field, 'DOM.dispatchKeyEvent',
                    { type: 'keyPressed', keyCode: 8, code: 0, flags: 0, key: 0, isChar: false });
  }
  await sleep(600);
}

async function typeName(text) {
  await typeInto(await nameField(), text);
  return (await nameField())?.props.Text === text;
}

// Picks New Profile… (the last item) in the open profile menu.
async function pickNewProfile(parent = null) {
  const menu = await waitFor(async () => openMenus(await viewTree()).find(
      m => m.root?.widget !== parent?.root?.widget), 5000);
  if (!menu) return false;
  const last = menu.items.at(-1);
  await rootMouse(menu.root, ['mouseMoved'], last.ax + last.w - 20, last.ay + last.h / 2, 'none');
  await rootMouse(menu.root, ['mousePressed', 'mouseReleased'], last.ax + last.w - 20,
                  last.ay + last.h / 2);
  return !!await waitFor(nameField, 5000);
}

// The dialog's checks, the same from both entry points; `name` is the
// profile made at the end. Returns whether the dialog closed on Create.
async function walkDialog(where, name) {
  step(`${where}: the New Profile dialog opens with an empty name, and Create is off`);
  await look();
  check(`${where}: Create is off with an empty name`, await createEnabled() === 'false',
        await createEnabled());

  step(`${where}: only spaces in the name: Create stays off`);
  await typeName('   ');
  await look();
  check(`${where}: Create is off with only spaces`, await createEnabled() === 'false',
        await createEnabled());
  // Create pressed anyway (Enter): nothing happens, the dialog stays.
  await typeInto(await nameField(), '', 13);
  check(`${where}: Enter with only spaces leaves the dialog open`, !!await nameField());
  await clearName();

  step(`${where}: a name is typed: Create turns on`);
  await typeName('Lab');
  await look();
  check(`${where}: Create is on after typing`, await createEnabled() === 'true',
        await createEnabled());
  await clearName();

  step(`${where}: a 65-character name, then Create: refused with a message, the dialog stays open`);
  const long = 'x'.repeat(65);
  check(`${where}: the 65-character name reached the field`, await typeName(long),
        (await nameField())?.props.Text?.length);
  await click(await createButton());
  const refused = await waitFor(() => message(TOO_LONG), 3000);
  await look(LOOK_MS * 2);
  check(`${where}: a 65-character name shows the too-long message`, !!refused,
        refused?.props.Text);
  check(`${where}: the dialog stays open after a refused name`, !!await nameField());

  step(`${where}: the name shortened: the message goes; Create makes "${name}"`);
  await clearName();
  check(`${where}: typing takes the message away`, !await message(TOO_LONG));
  await typeName(name);
  await look();
  await click(await createButton());
  const closed = await waitFor(async () => !await nameField(), 3000);
  check(`${where}: Create with a good name closes the dialog`, closed);
  return closed;
}

const [home] = await pages();

// --- The Create-a-Space form's Profile row.
step('Ctrl+Alt+N opens the Create-a-Space form; Profile ▸ New Profile…');
await key(home, 'n', CTRL | ALT, 800);
const formName = await waitFor(async () => (await viewTree()).find(
    v => v.cls === 'Textfield' && v.shown && v.path.includes('KoanSpaceCreationView')), 5000);
check('The Create-a-Space form opens', !!formName);
await typeInto(formName, 'Research');
await click(shown(await viewTree(), 'KoanSpaceProfileButton')[0]);
check('Profile ▸ New Profile… opens the dialog (form)', await pickNewProfile());
if (await walkDialog('Form', 'Lab')) {
  const row = await waitFor(async () => {
    const text = shown(await viewTree(), 'KoanSpaceProfileButton')[0]?.props.Text;
    return text === 'Lab' && text;
  }, 3000);
  step('The form\'s Profile row shows Lab at once; its menu has Lab ticked');
  check('The form\'s Profile row shows the new profile at once', row === 'Lab',
        shown(await viewTree(), 'KoanSpaceProfileButton')[0]?.props.Text);
  // Open the menu again for the video (its items paint their titles and
  // ticks: UI DevTools can't read them), and pick Lab again to close it.
  await click(shown(await viewTree(), 'KoanSpaceProfileButton')[0]);
  const menu = await waitFor(async () => openMenus(await viewTree())[0], 5000);
  await look(LOOK_MS * 2);
  if (menu) {
    // Default, Lab, New Profile… (separators aren't items).
    const lab = menu.items.at(-2);
    await rootMouse(menu.root, ['mouseMoved'], lab.ax + 30, lab.ay + lab.h / 2, 'none');
    await rootMouse(menu.root, ['mousePressed', 'mouseReleased'], lab.ax + 30, lab.ay + lab.h / 2);
  }
  await sleep(600);
  check('Lab is still the form\'s profile', shown(await viewTree(), 'KoanSpaceProfileButton')[0]
      ?.props.Text === 'Lab');
  step('Create Space: the Space Research is made with the profile Lab');
  await click(shown(await viewTree(), 'MdTextButton').find(
      v => v.props.Text === 'Create Space' && v.path.includes('KoanSpaceCreationView')));
  await look();
}

// --- Set Profile > New Profile… in a Space's menu.
step('A Space with no profile, Notes; its menu: Set Profile ▸ New Profile…');
await key(home, 'n', CTRL | ALT, 800);
const notesName = await waitFor(async () => (await viewTree()).find(
    v => v.cls === 'Textfield' && v.shown && v.path.includes('KoanSpaceCreationView')), 5000);
await typeInto(notesName, 'Notes', 13);
check('The Space Notes is made and active',
      !!await waitFor(async () => spaceButtons(await viewTree()).find(b => b.active)?.name === 'Notes',
                      5000));
const openSetProfile = async () => {
  const header = shown(await viewTree(), 'KoanSpaceHeaderView')[0];
  await viewMouse(header, 'mousePressed', 20, 20, 'right');
  await viewMouse(header, 'mouseReleased', 20, 20, 'right');
  const menu = await waitFor(async () => openMenus(await viewTree())[0], 5000);
  // Change Name…, Change Icon…, Theme…, Set Profile.
  if (!menu || menu.items.length < 4) return null;
  const item = menu.items[3];
  await rootMouse(menu.root, ['mouseMoved'], item.ax + 8, item.ay + item.h / 2, 'none');
  if (!await waitFor(async () => openMenus(await viewTree()).length > 1, 3000)) {
    await rootMouse(menu.root, ['mousePressed', 'mouseReleased'], item.ax + 8, item.ay + item.h / 2);
    if (!await waitFor(async () => openMenus(await viewTree()).length > 1, 3000)) return null;
  }
  return menu;
};
const spaceMenu = await openSetProfile();
check('The Space menu\'s Set Profile submenu opens', !!spaceMenu);
check('Set Profile ▸ New Profile… opens the dialog', !!spaceMenu && await pickNewProfile(spaceMenu));
if (await walkDialog('Set Profile', 'Desk')) {
  const desk = ((await readPref('koan.containers.user')) ?? []).find(c => c.name === 'Desk');
  const container = await waitFor(async () => ((await readPref('koan.workspaces.list')) ?? [])
      .find(s => s.title === 'Notes')?.container, 5000);
  check('The Space Notes has the new profile Desk at once',
        !!desk && container === `user:${desk.id}`, `${JSON.stringify(desk)} ${container}`);
  step('Set Profile again: Desk is ticked');
  const again = await openSetProfile();
  await look(LOOK_MS * 2);
  // Close both menus: pick Desk (ticked already) again.
  const sub = again && openMenus(await viewTree()).find(m => m.root?.widget !== again.root?.widget);
  if (sub) {
    const item = sub.items.at(-2);
    await rootMouse(sub.root, ['mouseMoved'], item.ax + item.w - 20, item.ay + item.h / 2, 'none');
    await rootMouse(sub.root, ['mousePressed', 'mouseReleased'], item.ax + item.w - 20,
                    item.ay + item.h / 2);
  }
  await look();
}

const profiles = ((await readPref('koan.containers.user')) ?? []).map(c => c.name);
check('Only the two profiles named were made (none for the refused names, none empty)',
      JSON.stringify(profiles) === JSON.stringify(['Lab', 'Desk']), JSON.stringify(profiles));
check('Nothing fatal in the log', !fatalInLog(), fatalInLog());
finish();
