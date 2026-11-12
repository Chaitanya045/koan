# Upstream audit: import from other browsers (#348)

Scope: what Koan's welcome import step and Chromium's import dialog list and import on macOS, and how Koan adds Zen
(and other Firefox-based browsers), Arc and Chrome (and the other Chromium-based ones). Pristine source:
`git -C ~/work/koan/chromium/src show HEAD:<path>` (Chromium 158). Zen 1.24b for behaviour:
`.cache/zen-desktop-1.24b/src/zen/migration/` (ZenProfileMigrators, ZenMigrationUtils), `src/zen/welcome/ZenWelcome.mjs`,
`src/browser/components/migration/*.patch` (read only, MPL).

## 1. Chromium 158's importer on macOS

| # | Piece | What it does (Mac) |
|---|---|---|
| I1 | `ImporterType` (`components/user_data_importer/common/importer_type.h`) | `TYPE_FIREFOX` 2, `TYPE_SAFARI` 3 (Mac), `TYPE_BOOKMARKS_FILE` 5; IE 0 and Edge 6 are Windows-only. No Chrome/Chromium type on any platform. |
| I2 | `ImporterList::DetectSourceProfiles` (`chrome/browser/importer/importer_list.cc`) | on the thread pool: Safari (`SafariImporterCanImport(~/Library)`) and Firefox (default browser first), then the interactive bookmarks file. The only list: the import dialog (`ImportDataHandler`), first-run auto-import (`first_run.cc`, profile 0, only with initial prefs asking for it) and Koan's welcome use it. |
| I3 | Firefox detection (`firefox_importer_utils*.cc`) | `GetProfilesINI()` = `DIR_APP_DATA/Firefox/profiles.ini` (`~/Library/Application Support/Firefox`); `GetFirefoxDetailsFromDictionary` lists `Profile0..N` (contiguous), relative paths against *Firefox's* ini folder (so it can't be pointed at Zen's); a profile is listed only if `compatibility.ini`'s `LastVersion` is 48 or more (Zen writes its own version, `1.23.3b_...`, which reads as 1). Name from `application.ini` (Firefox / Iceweasel). |
| I4 | What the Firefox importer brings on macOS | `HISTORY`, `FAVORITES`, `AUTOFILL_FORM_DATA`; **no `PASSWORDS`**: NSS can't be loaded into the signed process (comment in `DetectFirefoxProfiles`; `ImportPasswords` is `!IS_MAC`). Search engines and the home page are not offered. It reads only `source_path` (copies of places.sqlite, favicons.sqlite, formhistory.sqlite) and `app_path` for the default bookmarks to skip (empty: nothing skipped). |
| I5 | `ExternalProcessImporterHost` (`external_process_importer_host.cc`) | `StartImportSettings`: Firefox lock check (`FirefoxProfileLock` on `source_path/.parentlock`; a dialog asks to close the browser, or a headless import cancels), waits for the bookmark model and search engines, then `LaunchImportIfReady` starts the utility process (`ExternalProcessImporterClient`, `ProfileImport` mojo, kNoSandbox) with an `InProcessImporterBridge` to the `ProfileWriter`. |
| I6 | The utility process side (`chrome/utility/importer/importer_creator.cc`) | `CreateImporterByType`: bookmarks file, Firefox, Safari (Mac); anything else `NOTREACHED()`. The IPC traits (`profile_import_process_param_traits_macros.h`) take only `TYPE_UNKNOWN..TYPE_BOOKMARKS_FILE` off Windows. |
| I7 | A Chromium-profile importer | none on macOS in 158. (`edge_importer_win.cc` is old Edge's ESE database, Windows.) |
| I8 | The import dialog (`chrome/browser/resources/settings/people_page/import_data_dialog.*`) | a `<select>` of `importer_name` (+ profile), the first selected; checkboxes per `services_supported`. On macOS a `<select>` opens a native NSMenu: no icons possible without changing upstream's template. |
| I9 | `ProfileWriter` | bookmarks: straight onto an empty bookmarks bar, else into an "Imported from …" folder (the name the importer gives); history via `AddPagesWithDetails` with an imported visit source (all imported sources are treated alike, `visit_database.cc`); passwords to the profile store if saving is on. |
| I10 | `LogImporterUseToMetrics` | a switch on the type, no default: an unknown value counts as unknown. |

## 2. Decisions

| # | Upstream piece | Decision | Koan |
|---|---|---|---|
| D1 | The source list (I2) | **reuse + one hook**: `koan::importer::AddSourceProfiles(locale, profiles)` at the end of the worker, before the bookmarks file: Koan's sources first (the most recently used browser first, as Zen's welcome sorts by last use), then upstream's, then the welcome's pick moved to the front (`SetPreferredSource`). Everything that lists (dialog, welcome, auto-import) sees the same list. | `importer_list.cc.patch`, `koan_import_sources.cc` |
| D2 | Firefox detection (I3) | **redirect, no patch**: Koan parses Zen's (Floorp's, LibreWolf's, Waterfox's) profiles.ini itself with upstream's `DictionaryValueINIParser`: profiles with a places.sqlite, the install's `Default` (else `Default=1`) first, a single profile unnamed (as upstream). No version cutoff (Zen's version isn't Firefox's). | `ParseProfilesIni` |
| D3 | The Firefox importer (I4, I5) | **reuse**: a Zen source is `TYPE_FIREFOX` with Zen's profile folder, so the lock check, the utility process and the writer are upstream's. Passwords stay off on the Mac, as upstream decided (USAGE.md: Zen's CSV export). | — |
| D4 | No Chromium importer (I6, I7) | **override at one decision point**: `LaunchImportIfReady` asks `koan::importer::MaybeStartChromiumProfileImport` first; for a Koan Chromium source it reads the profile on the thread pool and writes through an `InProcessImporterBridge`, telling the host the progress as the utility process would. The type is `kChromiumImporterType` (7), never sent over IPC (I6's traits would reject it). Not a utility process importer: that needs patches to the enum, the IPC traits, `importer_creator.cc` and `importer_uma.cc` (its switch has no default), four hooks instead of one. | `external_process_importer_host.cc.patch`, `koan_chromium_profile_importer.cc`, `koan_chromium_profile_reader.cc` |
| D5 | Chromium profile layout | Local State's `profile.last_used`, `profiles_order`, `info_cache` (names); per profile `Bookmarks` + `AccountBookmarks` (JSON), `History` (`urls`), `Login Data` + `Login Data For Account` (`logins`), read from copies (the browser may hold them). Passwords: os_crypt's macOS scheme ("v10", PBKDF2-SHA1 of the "<Browser> Safe Storage" secret, "saltysalt", 1003, AES-128-CBC, 16-space IV), the secret from `crypto::apple::KeychainV2::FindGenericPassword` (macOS asks the user), asked only when the profile has a Login Data. Digest/federated logins, `chrome://` history and undecryptable passwords are left out. | reader |
| D6 | Arc's sidebar (`Arc/StorableSidebar.json`) | Zen turns it into Spaces, pinned tabs, folders and Essentials. Koan, for now, **as bookmarks**: the "global" container's Favorites (`topAppsContainerIDs`) in "Favorites", each Space's pinned container in a folder named after the Space (Arc's folders, split views flattened), per Arc profile (`profile.custom._0.directoryBasename`, else Default); unpinned tabs and non-web URLs left out. Spaces as Koan Spaces is a follow-up. | `ParseArcSidebar` |
| D7 | The dialog's `<select>` (I8) | **reuse**: names (and profile names) only; icons are the welcome's (`AppIconDataUrl`: NSWorkspace's icon of the app by bundle id, 64 px PNG data: URL; the page's CSP has no img-src limit). | welcome |
| D8 | The welcome's pick | **override**: the dialog opened on the first entry whatever was picked; now the pick is first (D1). | `KoanWelcomeHandler::HandleImport` |
| D9 | Tests reading real profiles | **lock**: `--koan-import-test-root=<dir>` replaces `~/Library/Application Support` and the keychain (`<dir>/Keychain/<item>` files); every test uses it. | `koan_switches.h` |

## 3. What Zen suppresses in Firefox, for import

| Firefox | Zen | Chromium equivalent | Koan |
|---|---|---|---|
| The migration wizard's browser list | adds Arc and Dia migrators (`ChromeMigrationUtils` paths `Arc/User Data`, `Dia/User Data`; keychain "Arc Safe Storage", "Dia Safe Storage"), an "All profiles" entry | the dialog's list | D1; no "All profiles" (one import per profile) |
| Bookmarks import into the toolbar | `zen.migration.bookmarks-as-pinned-folders`: bookmarks become pinned folders | `ProfileWriter` (bar or "Imported from" folder) | upstream's |

## 4. Tests

- Unit (`koan_unittests`): `koan_import_sources_unittest.cc` (profiles.ini, Local State, detection order and services, the hook and the welcome's pick), `koan_chromium_profile_reader_unittest.cc` (the key and a fixed "v10" vector from other code, Bookmarks JSON, Arc's sidebar, History, Login Data).
- Browser (`koan_browsertests`): `koan_import_browsertest.cc`: the list, the pick first, Zen through the Firefox importer, Chrome with its password, Arc's pinned tabs, only the items asked.
- E2E (Mac suite): `scripts/e2e/import_sources.mjs`: the welcome's list with icons, the dialog on the pick, imports, and what landed (bookmarks, history, passwords pages).
