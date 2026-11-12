// Setup of import_sources.mjs, run by scripts/mac/suite.sh and
// scripts/e2e/run.sh before Koan starts (scripts/e2e/setup/<name>.mjs, with
// KOAN_PROFILE set): writes the fixture profiles under the run's test root.
import { importRoot, writeImportFixtures } from '../lib/import_fixtures.mjs';

if (!process.env.KOAN_PROFILE) {
  console.error('setup/import_sources.mjs: KOAN_PROFILE is not set');
  process.exit(1);
}
const status = await writeImportFixtures(importRoot(process.env.KOAN_PROFILE));
console.log(`import fixtures: ${JSON.stringify(status)}`);
