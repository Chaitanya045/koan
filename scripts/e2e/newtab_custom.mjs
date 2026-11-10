// End-to-end test: a new tab page set in the profile (here the pref that
// Chromium's NewTabPageLocation policy sets, seeded by
// prefs/newtab_custom.json) shows instead of Koan's page.
//
//   scripts/e2e/run.sh newtab_custom
import { browserCall, check, evalIn, finish, pages, waitFor } from './lib/koan.mjs';

const targetId = (await browserCall('Target.createTarget',
    { url: 'chrome://newtab', background: true })).result.targetId;
let href;
await waitFor(async () => {
  const page = (await pages()).find(p => p.id === targetId);
  href = page && await evalIn(page, 'location.href');
  return href?.startsWith('https://');
});
check('A custom new tab page replaces Koan\'s',
      href?.startsWith('https://example.com/'), href);
await browserCall('Target.closeTarget', { targetId });
finish();
