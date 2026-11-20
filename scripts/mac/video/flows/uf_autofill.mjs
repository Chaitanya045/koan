// User flow (stub until feat/agent-autofill, #412, is merged): agents use
// the person's autofill but never read the secrets. Test cases UF-AFL-* in
// .agent/notes/user-flows.md. Skips (exit 77) when this build's koan has no
// `autofill` command. Otherwise: the person signs in on a site and saves the
// password (a real click on Save), the agent opens the same site, lists the
// suggestions for the field (labels only: no password, not even its
// length), picks one (Chrome's own fill), and nothing the agent can read
// (snapshot, --full, --json, a screenshot's line) carries the secret, while
// the page itself has it.
import fs from 'node:fs';
import { CLI, begin, brief, browserCall, clickElement, clickView, end, evalIn, keyPress, koan, lastLine, loc,
         openAnchor, pageById, pair, parkPointer, recalibrate, show, skipFlow, sleep, step, tabIdOk, viewTree,
         waitFor } from './uf_lib.mjs';

const fixtures = await begin('uf_autofill', 'Agents use the person\'s autofill and never read the secrets (#412, stub)',
                             '#412');
if (!CLI || !fs.existsSync(CLI)) skipFlow(`no koan CLI (KOAN_CLI=${CLI ?? ''})`);
const help = await koan(['help', 'autofill']);
if (help.code !== 0 || !/--list/.test(help.out)) {
  skipFlow('this build has no `koan autofill` (feat/agent-autofill, #412, not merged yet)');
}
const SECRET = 'Tr0ub4dor&3-uf';
const anchor = await openAnchor(fixtures.url('/secret'));
await parkPointer();
let tab = null;

await step('UF-AFL-1', 'The person signs in and saves the password (a real click on Save)', async t => {
  const page = await pageById(anchor.id);
  await recalibrate(page);
  let c = await clickElement(page, 'document.getElementById("name")', 200);
  for (const ch of 'ada') await keyPress(ch, '', 60);
  c = c.ok && await clickElement(page, 'document.getElementById("pass")', 200);
  // (The password typed as the person would; keypress2 takes a key code per
  // character, so the field is set by the page for the rest of it.)
  await evalIn(page, `document.getElementById('pass').value = ${JSON.stringify(SECRET)};
      document.getElementById('pass').dispatchEvent(new Event('input', {bubbles: true}))`);
  await keyPress('Enter', '', 1500);
  const save = await waitFor(async () => {
    const vs = await viewTree();
    const b = vs.find(v => v.shown && v.cls === 'MdTextButton' && /^Save$/.test(v.props.Text ?? ''));
    return b && { vs, b };
  }, 8000);
  t.check('Chrome asks to save the password', !!save);
  if (save) {
    await t.shot('save-bubble');
    const s = await clickView(save.b, save.vs, 800);
    t.check('a real click on Save', s.ok, s.why);
  }
}, { needs: ['real input'] });

await step('UF-AFL-2', 'The agent lists the field\'s suggestions: labels only, never the password', async t => {
  const { r, approval } = await pair(t, 'UF agent');
  t.check('paired with a real click on Allow', r.code === 0 && approval?.real, approval?.why ?? brief(r));
  const o = await koan(['open', fixtures.url('/secret')]);
  tab = lastLine(o);
  t.check('koan open', o.code === 0 && tabIdOk(tab), brief(o));
  await sleep(1500);
  const l = await koan(['autofill', tab, loc('textbox', 'Name'), '--list']);
  t.check('--list names the saved login', l.code === 0 && /ada/.test(l.out) && /saved password/.test(l.out), brief(l));
  t.check('...and never the password, nor dots as long as it', !l.out.includes(SECRET) &&
          !new RegExp(`[•·*]{${SECRET.length}}`).test(l.out), l.out);
});

await step('UF-AFL-3', 'The agent picks it: Chrome fills the form, the agent sees "•••• (filled)"', async t => {
  const p = await koan(['autofill', tab, loc('textbox', 'Name'), '--pick', '1']);
  t.check('--pick 1 fills', p.code === 0, brief(p));
  const page = await pageById(tab);
  const value = page && await evalIn(page, 'document.getElementById("pass").value');
  t.check('the page has the password (Chrome filled it)', value === SECRET, value ? '(a value, not printed)' : 'empty');
  const snap = await koan(['snapshot', tab, '--interactive']);
  t.check('the snapshot shows the field as "•••• (filled)"', /textbox "Password".*•••• \(filled\)/.test(snap.out), snap.out.slice(0, 300));
  const full = await koan(['snapshot', tab, '--full']);
  const json = await koan(['snapshot', tab, '--json']);
  t.check('no snapshot (compact, --full, --json) carries the secret',
          ![snap, full, json].some(x => (x.out + x.err).includes(SECRET)));
  await show(tab);
  await t.shot('filled');
  await show(anchor.id);
});

await step('UF-AFL-4', 'The agent ends', async t => {
  const e = await koan(['end']);
  t.check('koan end', e.code === 0, brief(e));
  await browserCall('Target.activateTarget', { targetId: anchor.id }).catch(() => {});
});

end();
