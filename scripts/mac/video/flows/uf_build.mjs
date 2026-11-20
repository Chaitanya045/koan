// User flow: the built app (#393, #394, #395, #405). Test cases UF-BLD-* in
// .agent/notes/user-flows.md. Only what a person installing the app
// relies on: it is signed with a stable identity (a designated requirement
// on the certificate, not on this build's hash, so the Keychain item Koan
// saved keeps matching after an update), the signature verifies with the
// Widevine VMP signature in place (VMP signing before code signing), and
// Widevine is on (the CDM is Google's: Chromium can't bundle it unbranded,
// so the component updater downloads it into the profile, build/koan-mac.gn;
// the bundle carries the VMP signature it checks). KOAN_BUILD_APP
// (run_flows.sh --build-app) is the app whose signature is checked, by
// default KOAN_APP, the app running. A dev build (out/Koan) is ad hoc signed:
// the signature steps SKIP on it, saying so.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { begin, end, evalIn, info, openAnchor, pageById, skipFlow, sleep, step, waitFor } from './uf_lib.mjs';
import { browserCall } from '../../../e2e/lib/koan.mjs';

await begin('uf_build', 'The app is signed with a stable identity and has Widevine', '#393 #394 #395 #405');
const APP = process.env.KOAN_BUILD_APP || process.env.KOAN_APP;
if (!APP || !fs.existsSync(path.join(APP, 'Contents/Info.plist'))) {
  skipFlow(`no app (KOAN_BUILD_APP=${process.env.KOAN_BUILD_APP ?? ''}, KOAN_APP=${process.env.KOAN_APP ?? ''})`);
}
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

// Ad hoc: a dev build, which is never signed with an identity (no signing
// identity on the bench either): the signature steps don't apply to it.
const signature = run('codesign', ['-dv', '--verbose=4', APP]);
const ADHOC = /Signature=adhoc/.test(signature.out) ?
  `${path.basename(path.dirname(APP))}/${path.basename(APP)} is ad hoc signed (a dev build, never signed with an identity): ` +
  'run_flows.sh --build-app <a signed Koan.app> (a release, or the official build once signed) checks it' : '';
info(`signature: ${signature.out.split('\n').filter(l => /Signature|Authority|TeamIdentifier/.test(l)).join(' | ')}`);

await step('UF-BLD-1', 'The app\'s signature verifies (deep, strict)', async t => {
  if (ADHOC) return t.skip(ADHOC);
  const r = run('codesign', ['--verify', '--deep', '--strict', '--verbose=2', APP]);
  t.check('codesign --verify --deep --strict', r.code === 0, r.out.split('\n').slice(0, 3).join(' | '));
});

await step('UF-BLD-2', 'It is signed with a stable identity, not ad hoc', async t => {
  if (ADHOC) return t.skip(ADHOC);
  const r = signature;
  t.check('signed by an identity (an Authority line), not "Signature=adhoc"',
          /^Authority=/m.test(r.out) && !/Signature=adhoc/.test(r.out), r.out.split('\n').filter(l => /Authority|Signature|TeamIdentifier/.test(l)).join(' | '));
  const req = run('codesign', ['-d', '-r-', APP]);
  t.check('its designated requirement names the certificate (stays the same across builds), not a cdhash',
          /certificate leaf|anchor /.test(req.out) && !/cdhash H"/.test(req.out), req.out.split('\n').filter(l => /designated/.test(l)).join(' '));
  info(`signer: ${(r.out.match(/^Authority=(.*)$/m) ?? [])[1] ?? '?'}`);
});

await step('UF-BLD-3', 'Widevine: the VMP signature is in the bundle (signed before the code), the CDM a component', async t => {
  // The framework's VMP signature (scripts/mac/widevine_vmp_sign.sh), which
  // the CDM checks (enable_cdm_host_verification).
  const sigs = find(APP, p => /Koan Framework\.sig$/.test(p), 7);
  t.check('the framework\'s VMP signature (Koan Framework.sig) is in the bundle', sigs.length > 0,
          sigs.map(p => path.relative(APP, p)).join(', ') || 'none');
  // Not bundled: Chromium can't ship Google's CDM unbranded (build/koan-mac.gn).
  const libs = find(path.join(APP, 'Contents/Frameworks'), p => /libwidevinecdm\.dylib$/.test(p));
  info(`Widevine CDM in the bundle: ${libs.length ? libs.map(p => path.relative(APP, p)).join(', ') : 'none (the component updater downloads it, as designed)'}`);
  if (ADHOC) {
    t.check('(the code signature with the VMP signature in place: checked on a signed app)', true, ADHOC);
  } else {
    const r = run('codesign', ['--verify', '--deep', '--strict', APP]);
    t.check('with the VMP .sig in the bundle, the code signature still verifies (VMP first)', r.code === 0, r.out);
  }
});

await step('UF-BLD-4', 'The running app says what it is (chrome://version)', async t => {
  const anchor = await openAnchor('chrome://version/');
  const p = await waitFor(() => pageById(anchor.id), 5000);
  await sleep(800);
  const text = p ? await evalIn(p, 'document.body.innerText') : '';
  t.check('chrome://version names Koan and Chromium 158', /Koan/.test(text) && /158\./.test(text), String(text).slice(0, 200));
  await t.shot('version');
  // Widevine is built in (enable_widevine): its component is registered,
  // and the component updater fetches the CDM into the profile.
  const comp = await openAnchor('chrome://components/');
  const c = await waitFor(() => pageById(comp.id), 5000);
  const listed = c && await waitFor(async () => /Widevine Content Decryption Module/.test(
      await evalIn(c, 'document.body.innerText') ?? '') || null, 6000);
  t.check('chrome://components lists the Widevine Content Decryption Module', !!listed,
          c ? String(await evalIn(c, 'document.body.innerText')).slice(0, 200) : 'no page');
  await browserCall('Target.activateTarget', { targetId: anchor.id }).catch(() => {});
});

end();
