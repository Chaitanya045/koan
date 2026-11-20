// User flow: the built app (#393, #394, #395, #405). Test cases UF-BLD-* in
// .agent/notes/user-flows.md. Only what a person installing the app
// relies on: it is signed with a stable identity (a designated requirement
// on the certificate, not on this build's hash, so the Keychain item Koan
// saved keeps matching after an update), the signature verifies with the
// Widevine VMP signature in place (VMP signing before code signing), and
// the Widevine CDM is in the bundle. KOAN_APP is the app under test.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { begin, end, evalIn, info, openAnchor, pageById, skipFlow, sleep, step, waitFor } from './uf_lib.mjs';
import { browserCall } from '../../../e2e/lib/koan.mjs';

await begin('uf_build', 'The app is signed with a stable identity and has Widevine', '#393 #394 #395 #405');
const APP = process.env.KOAN_APP;
if (!APP || !fs.existsSync(path.join(APP, 'Contents/Info.plist'))) skipFlow(`no app (KOAN_APP=${APP ?? ''})`);
const run = (cmd, args) => {
  try {
    const out = execFileSync(cmd, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], timeout: 120000 });
    return { code: 0, out };
  } catch (e) {
    return { code: e.status ?? -1, out: `${e.stdout ?? ''}${e.stderr ?? ''}` };
  }
};
const find = (dir, test, depth = 8) => {
  const hits = [];
  const walk = (d, n) => {
    if (n > depth) return;
    let entries = [];
    try { entries = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of entries) {
      const p = path.join(d, e.name);
      if (test(p, e)) hits.push(p);
      if (e.isDirectory() && !e.isSymbolicLink()) walk(p, n + 1);
    }
  };
  walk(dir, 0);
  return hits;
};

await step('UF-BLD-1', 'The app\'s signature verifies (deep, strict)', async t => {
  const r = run('codesign', ['--verify', '--deep', '--strict', '--verbose=2', APP]);
  t.check('codesign --verify --deep --strict', r.code === 0, r.out.split('\n').slice(0, 3).join(' | '));
});

await step('UF-BLD-2', 'It is signed with a stable identity, not ad hoc', async t => {
  const r = run('codesign', ['-dv', '--verbose=4', APP]);
  t.check('signed by an identity (an Authority line), not "Signature=adhoc"',
          /^Authority=/m.test(r.out) && !/Signature=adhoc/.test(r.out), r.out.split('\n').filter(l => /Authority|Signature|TeamIdentifier/.test(l)).join(' | '));
  const req = run('codesign', ['-d', '-r-', APP]);
  t.check('its designated requirement names the certificate (stays the same across builds), not a cdhash',
          /certificate leaf|anchor /.test(req.out) && !/cdhash H"/.test(req.out), req.out.split('\n').filter(l => /designated/.test(l)).join(' '));
  info(`signer: ${(r.out.match(/^Authority=(.*)$/m) ?? [])[1] ?? '?'}`);
});

await step('UF-BLD-3', 'The Widevine CDM is in the bundle (and its VMP signature, if any, came first)', async t => {
  const libs = find(path.join(APP, 'Contents/Frameworks'), p => /libwidevinecdm\.dylib$/.test(p));
  const manifests = find(path.join(APP, 'Contents/Frameworks'), p => /WidevineCdm\/manifest\.json$/.test(p));
  t.check('WidevineCdm with its library and manifest', libs.length > 0 && manifests.length > 0,
          `${libs.length} libraries, ${manifests.length} manifests`);
  let version = '';
  try { version = JSON.parse(fs.readFileSync(manifests[0], 'utf8')).version; } catch {}
  t.check('the manifest has a version', !!version, version);
  const sigs = find(APP, p => /\.sig$/.test(p), 7);
  info(`Widevine ${version}; VMP signatures: ${sigs.length ? sigs.map(p => path.relative(APP, p)).join(', ') : 'none (no VMP signing on this build)'}`);
  if (sigs.length) {
    const r = run('codesign', ['--verify', '--deep', '--strict', APP]);
    t.check('with the VMP .sig files in the bundle, the code signature still verifies (VMP first)', r.code === 0, r.out);
  }
});

await step('UF-BLD-4', 'The running app says what it is (chrome://version)', async t => {
  const anchor = await openAnchor('chrome://version/');
  const p = await waitFor(() => pageById(anchor.id), 5000);
  await sleep(800);
  const text = p ? await evalIn(p, 'document.body.innerText') : '';
  t.check('chrome://version names Koan and Chromium 158', /Koan/.test(text) && /158\./.test(text), String(text).slice(0, 200));
  await t.shot('version');
  await browserCall('Target.activateTarget', { targetId: anchor.id }).catch(() => {});
});

end();
