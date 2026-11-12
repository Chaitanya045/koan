// End-to-end test of Widevine DRM (issue #315) against a real Mac build:
// the build has the Widevine CDM component (enable_widevine), the component
// updater downloads the CDM into the fresh profile, and then an https page
// gets the com.widevine.alpha key system from
// navigator.requestMediaKeySystemAccess() and can create MediaKeys (which
// loads the CDM in its utility process).
//
//   scripts/mac/suite.sh --all-e2e --only widevine --skip-cxx --skip-visual <Koan.app>
//
// Needs the network: the CDM comes from Google's component update server
// (about 10 MB), so it is not in scripts/mac/suite/e2e.list. widevine.flags
// asks suite.sh to leave the component updater on. Whether the app is
// VMP-signed isn't visible to a page; the log line the CDM prints is
// reported for information (docs/USAGE.md, "DRM video", has the manual
// checks with real DRM content).
import { browserCall, check, evalIn, finish, IS_MAC, pages, sleep, waitFor } from './lib/koan.mjs';
import fs from 'node:fs';

if (!IS_MAC) {
  // Koan enables Widevine in build/koan-mac.gn only.
  console.log('SKIP not macOS: Widevine is enabled in the Mac build only');
  process.exit(77);
}

// The Widevine CDM component's id, as chrome://components shows it (made
// from kWidevineSha2Hash in widevine_cdm_component_installer.cc).
const WIDEVINE_ID = 'oimompecagnajdejgnnjijobebaeigek';
const KEY_SYSTEM = 'com.widevine.alpha';

async function openTab(url) {
  const targetId = (await browserCall('Target.createTarget', { url, background: true }))
      .result.targetId;
  const page = await waitFor(async () => (await pages()).find(p => p.id === targetId));
  await waitFor(() => evalIn(page, 'document.readyState === "complete"'), 20000);
  return { page, close: () => browserCall('Target.closeTarget', { targetId }) };
}

const components = await openTab('chrome://components');
const widevine = () => evalIn(components.page, `
    import('chrome://resources/js/cr.js')
      .then(m => m.sendWithPromise('requestComponentsData'))
      .then(d => d.components.find(c => c.id === '${WIDEVINE_ID}') ?? null)`);

let cdm = await waitFor(widevine, 10000);
check('chrome://components lists the Widevine CDM component',
      cdm && /Widevine/.test(cdm.name), JSON.stringify(cdm));
if (!cdm) finish();

// A fresh profile has no CDM (version 0.0.0.0). The updater's first check
// waits a minute or so after start; ask for it now, as the page's "Check for
// update" button does, and wait for the download.
const installed = c => c && c.version && c.version !== '0.0.0.0';
if (!installed(cdm)) {
  await evalIn(components.page, `chrome.send('checkUpdate', ['${WIDEVINE_ID}'])`);
}
cdm = await waitFor(async () => {
  const c = await widevine();
  return installed(c) && c;
}, 240000) || await widevine();
check('...and the component updater installs it', installed(cdm), JSON.stringify(cdm));
await components.close();

const site = await openTab('https://example.com/');
const config = JSON.stringify([{
  initDataTypes: ['cenc'],
  audioCapabilities: [{ contentType: 'audio/mp4; codecs="mp4a.40.2"' }],
  videoCapabilities: [{ contentType: 'video/mp4; codecs="avc1.42E01E"',
                        robustness: 'SW_SECURE_DECODE' }],
}]);
const access = () => evalIn(site.page, `
    navigator.requestMediaKeySystemAccess('${KEY_SYSTEM}', ${config})
      .then(a => a.createMediaKeys().then(() => ({ keySystem: a.keySystem, keys: true }),
                                          e => ({ keySystem: a.keySystem, error: String(e) })))
      .catch(e => ({ error: String(e) }))`);
// Renderers learn about a newly installed CDM asynchronously
// (components/cdm/renderer/key_system_support_update.cc): retry for a while.
let result = await waitFor(async () => {
  const r = await access();
  return r?.keys && r;
}, 30000) || await access();
check(`requestMediaKeySystemAccess('${KEY_SYSTEM}') resolves`,
      result?.keySystem === KEY_SYSTEM, JSON.stringify(result));
check('...and createMediaKeys() loads the CDM', result?.keys === true, JSON.stringify(result));
await site.close();

// VMP (information only): the CDM logs a failed host verification when the
// framework's signature is the build's placeholder.
await sleep(1000);
const log = process.env.KOAN_LOG && fs.existsSync(process.env.KOAN_LOG)
  ? fs.readFileSync(process.env.KOAN_LOG, 'utf8') : '';
console.log(/Failed to verify CDM host/.test(log)
  ? 'INFO  VMP: the CDM could not verify the host (no VMP signature; Netflix will refuse)'
  : 'INFO  VMP: no host-verification failure in the log');
finish();
